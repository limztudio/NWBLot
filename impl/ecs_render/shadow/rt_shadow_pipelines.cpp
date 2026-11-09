// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <core/task/gpu/task_graph.h>
#include <impl/global.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/shadow/types.adhd.h>
#include <core/alloc/scratch.h>
#include <impl/ecs_render/raytrace/prepared_builds.h>
#include <impl/ecs_render/raytrace/prepared_software_bvh_graph_resources.h>
#include <core/graphics/gpu_timing.h>
#include <impl/ecs_render/shadow/light_space_plan.adhd.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/shadow/quality_settings.adhd.h>
#include <core/graphics/rhi/foundation.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include "light_space_shadow.h"
#include "light_space_resource_set.h"
#include <impl/ecs_render/shadow/light_space_shadow_diagnostics.adhd.h>
#include <impl/ecs_render/shadow/light_space_capture_identity_builder.adhd.h>
#include <core/graphics/backend_selection/backend.h>
#include <global/hash_utils.h>
#include <global/limit.h>
#include "light_space_csg.h"
#include <impl/ecs_render/shadow/light_space_csg_context_packer.adhd.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/assets_graphics/shadow_subsystem.adhd.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/shadow/light_space_settings.adhd.h>
#include <core/graphics/shader_archive.h>
#include "light_space_scene_gather.h"
#include <impl/ecs_render/raytrace/raytracing_scene_identity_builder.adhd.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>
#include "task_graph_light_space_shadow.h"
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/ecs_render/shadow/shadow_prepare_graph_coordinator.h>
#include <impl/ecs_render/raytrace/software_scene_refit.h>
#include <core/task/gpu/capture/command_ir.h>
#include <global/timer.h>
#include "shadow_prepare_packet_validator.h"
#include <impl/ecs_render/renderer_frame_pipeline.h>
#include "shadow_trace_geometry.h"
#include <global/overflow.h>
#include "shadow_material_context_preparer.h"
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <impl/ecs_render/shadow/slot_selection.adhd.h>
#include <impl/ecs_render/shadow/shadow_visibility_graph_coordinator.h>
#include <impl/ecs_render/kernel/task_graph_float_clear_tasks.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include "shadow_visibility_merge_validator.h"
#include "shadow_visibility_target_builder.h"
#include "soft_shadow_resolve_dispatch_builder.h"
#include <impl/ecs_render/shadow/task_graph_light_space_shadow.h>
#include <impl/ecs_render/shadow/shadow_pipeline_set.h>
#include <impl/ecs_render/shadow/soft_shadow_resolve_dispatch_builder.h>
#include <impl/ecs_render/shadow/soft_shadow_wavelet.h>
#include <global/range_algorithms.h>
#include "hardware_transparent_shadow_state.h"
#include "hardware_transparent_shadow_resources.h"
#include <impl/assets/graphics/shadow/hardware_transparent_binding_slots.h>
#include "shadow_occluder_material_gather.h"
#include "prepared_shadow_material_context.h"
#include <impl/ecs_render/shadow/shadow_material_context_policy.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SoftShadowSplitResourceInputs{
    const DeferredFrameTargets& targets;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan;
    const RayTracingSceneGraphReads& hardwareSceneReads;
    Span<const Core::GpuGraphResourceId> hardwareTransparentScratch{};
    SoftShadowSplitRoute route;
    Span<const Core::GpuGraphResourceId> traceGeometryResources{};
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId shadowVisibility;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId materialContextSlots;
    Core::GpuGraphResourceId sceneGeometryDomain;
    Core::GpuGraphResourceId sceneTlas;
};

// Per-callback resource declarations of the prepared soft-transparent shadow route.
struct SoftShadowSplitResourceUses{
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaque;
    Array<Core::GpuTaskResourceUse, s_MaxOpaqueFirstWaveletUses> opaqueFirstWavelet{};
    usize opaqueFirstWaveletCount = 0u;
    Array<Core::GpuTaskResourceUse, s_MaxOpaqueResolveUses> opaqueResolve{};
    usize opaqueResolveCount = 0u;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentTrace;
    Array<Core::GpuTaskResourceUse, s_MaxTransparentTemporalMergeUses> transparentTemporalMerge{};
    usize transparentTemporalMergeCount = 0u;
    Array<Core::GpuTaskResourceUse, s_MaxTransparentFirstWaveletUses> transparentFirstWavelet{};
    usize transparentFirstWaveletCount = 0u;
    Array<Core::GpuTaskResourceUse, s_MaxTransparentFoldUses> transparentFold{};
    usize transparentFoldCount = 0u;
    b8 graphOwnsTransparentTemporalMergeEntryStates = false;


public:
    explicit SoftShadowSplitResourceUses(Core::Alloc::ScratchArena& scratchArena)noexcept
        : opaque(scratchArena)
        , transparentTrace(scratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Re-imports the soft-shadow targets under their shared graph identities and declares each split callback's exact states.
class SoftShadowSplitResourceUseBuilder final : NoCopy{
public:
    explicit SoftShadowSplitResourceUseBuilder(Core::GpuTaskGraph& graph)noexcept
        : m_graph(graph)
    {}


public:
    [[nodiscard]] b8 build(const SoftShadowSplitResourceInputs& inputs, SoftShadowSplitResourceUses& uses);


private:
    Core::GpuTaskGraph& m_graph;
};


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


public:
    ShadowPrepareGeometryResources(const ShadowPrepareGeometryInputs& inputs, Core::Alloc::ScratchArena& scratchArena)noexcept
        : m_blasBuildInputs(scratchArena)
        , m_remainingTraceGeometry(scratchArena)
        , m_inputs(inputs)
        , m_scratchArena(scratchArena)
    {}


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
    Array<InlineRequest, s_SmallInlineIndexCapacity> m_inlineRequests = {};
    usize m_inlineRequestCount = 0u;
    Optional<RequestIndex> m_requests;
    // Request storage is complete before the index retains pointers; it never grows after.
    Optional<ResourceIndex> m_resources;
    u64 m_graphGeneration = 0u;
    // Prepared build names stay frozen and outlive this operation; membership borrows them.
    mutable Array<const NameHash*, s_SmallInlineIndexCapacity> m_inlineNames = {};
    mutable usize m_inlineNameCount = 0u;
    mutable Optional<NameIndex> m_names;
    b8 m_preparedBlasPolicy = false;
    b8 m_storagePrepared = false;
    b8 m_inputsGathered = false;
    mutable b8 m_namesPrepared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPrepareUploadInputs{
    const DeferredFrameTargets& targets;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId materialContextSlots;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Frozen selector, context, targets, constants, and scene-BVH uploads precede Shadow Preparation, in that order.
// Tasks declared before a failure stay in `chain`.
class ShadowPrepareUploadChainBuilder final : NoCopy{
public:
    ShadowPrepareUploadChainBuilder(Core::GpuTaskGraph& graph, Core::GraphicsRuntime& graphics, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_graphics(graphics)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] b8 declare(const ShadowPrepareUploadInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareUploadChain& chain){
        return declareSelectorAndContextUploads(inputs, scratchArena, chain) && declareSurfelFrameConstantsUpload(inputs, chain) && declareShadowMaterialContextUploads(inputs, chain) && declareSceneBvhUploads(inputs, chain);
    }


private:
    [[nodiscard]] b8 declareSelectorAndContextUploads(const ShadowPrepareUploadInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareUploadChain& chain);
    [[nodiscard]] b8 declareSurfelFrameConstantsUpload(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);
    [[nodiscard]] b8 declareShadowMaterialContextUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);
    [[nodiscard]] b8 declareSceneBvhUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain);


private:
    Core::GpuTaskGraph& m_graph;
    Core::GraphicsRuntime& m_graphics;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;
class RendererMeshSystem;
struct RayTracingShadowPreparationResourceSnapshot;
class ShadowPrepareUploadChain;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPrepareResourceInputs{
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources;
    const ShadowPrepareUploadChain& uploads;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId materialContextSlots;
    Span<const Core::GpuGraphResourceId> shadowTraceGeometryResources;
    Span<const Core::GpuGraphResourceId> softwareBvhBuildStateResources;
    Span<const PreparedMeshBlasBuild> preparedMeshBlasBuilds;
    b8 sceneTlasBuildGraphOwned;
    b8 meshBlasBuildsGraphOwned;
    b8 meshBlasGeometryBuildInputStatesGraphOwned;
};

struct ShadowPrepareResourcePlan{
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses;
    Array<Core::GpuTaskResourceSetUse, s_ShadowPrepareResourceSetUseCapacity> resourceSetUses = {};
    usize resourceSetUseCount = 0u;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accelStructFinalizeResourceUses;
    Core::GpuGraphResourceId sceneTlas;


public:
    explicit ShadowPrepareResourcePlan(Core::Alloc::ScratchArena& scratchArena)noexcept
        : resourceUses(scratchArena)
        , accelStructFinalizeResourceUses(scratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowPrepareResourcePlanner final : NoCopy{
public:
    ShadowPrepareResourcePlanner(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem, RendererMeshSystem& meshSystem)noexcept
        : m_graph(graph)
        , m_raytracingSystem(raytracingSystem)
        , m_meshSystem(meshSystem)
    {}


public:
    [[nodiscard]] b8 prepare(const ShadowPrepareResourceInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareResourcePlan& result);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
    RendererMeshSystem& m_meshSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowSoftwareBvhBuildChain final : NoCopy{
public:
    ShadowSoftwareBvhBuildChain(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] b8 declare(
        Span<const PreparedMeshSwBvhGraphResources> pureSoftwareMeshSwBvhGraphResources,
        Core::GpuTimingSubmissionTicket& timingTicket,
        Core::GpuTaskId& shadowPrepareDependency,
        Core::GpuTaskId& firstTask,
        Core::GpuTaskId& lastTask
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowAccelerationFinalizeBuilder final : NoCopy{
public:
    explicit ShadowAccelerationFinalizeBuilder(Core::GpuTaskGraph& graph)noexcept
        : m_graph(graph)
    {}


public:
    [[nodiscard]] b8 declare(
        Core::Alloc::ScratchArena& scratchArena,
        b8 sceneTlasBuildGraphOwned,
        Core::GpuGraphResourceId sceneTlas,
        usize preparedMeshBlasBuildCount,
        Span<const Core::GpuTaskResourceUse> accelStructFinalizeResourceUses,
        const Core::GpuTaskId& prepareTask,
        Core::GpuTaskId& outTask
    );


private:
    Core::GpuTaskGraph& m_graph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPrepareGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, true };


public:
    // All bindings required: the prepare task always preflights this frame's shadow resources.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        ShadowPreparationOutcome& outcome;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        b8 deferredBindlessSlotsWereUploaded = false;
        b8 currentBindlessSlotsGraphOwned = false;
        b8 shadowMaterialContextBatchGraphOwned = false;
        b8 sceneBvhBatchGraphOwned = false;
        b8 sceneTlasBuildGraphOwned = false;
        b8 meshBlasBuildsGraphOwned = false;
        b8 meshBlasGeometryBuildInputStatesGraphOwned = false;
        b8 meshSwBvhBuildsGraphOwned = false;
        b8 preparedMeshSwBvhBuildsRecordedByGraph = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept;
    static void Discarded(Payload& payload)noexcept;
};


// Software preparation shares scratch across frozen mesh builds. Keep each sentinel by its callback so meshes never clear each other.
struct ShadowPrepareSoftwareBvhBuildGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // The raytracing system and the timing ticket are required: every enqueued software BVH build records against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GpuTimingSubmissionTicket& timingTicket;
        PreparedMeshSwBvhBuild build;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SoftShadowSplitTaskChainInputs{
    DeferredFrameTargets& targets;
    const DeferredLightingGraphResources& deferredLightingResources;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan;
    const b8& shadowPrepared;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& asyncTiming;
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming;
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming;
    b8& opaqueProduced;
    b8& transparentTraceProduced;
    u32& opaqueFrameIndex;
    SoftShadowSplitRoute route;
    Span<const Core::GpuGraphResourceId> traceGeometryResources{};
    Span<const Core::GpuTaskResourceSetUse> traceResourceSetUses{};
    Core::GpuTaskResourceSetUse traceGeometrySetUse{};
    Span<const Core::GpuTaskExternalStateSource> stateSources{};
    Span<const Core::GpuExternalCompletionId> historyWriterDrainDependencies{};
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId materialContextSlots;
    Core::GpuTaskId prefixTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Prepared soft-transparent route: light-space maps, opaque producer with wavelet/resolve, transparent trace, merge, wavelet, fold.
// Tasks declared before a failure stay in `tasks`.
class SoftShadowSplitTaskChainBuilder final : NoCopy{
private:
    using LightSpaceShadowMaps = SoftShadowLightSpaceMaps;


public:
    SoftShadowSplitTaskChainBuilder(Core::GpuTaskGraph& graph, Core::GraphicsRuntime& graphics, Core::Alloc::GlobalArena& arena, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_graphics(graphics)
        , m_arena(arena)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] b8 declare(const SoftShadowSplitTaskChainInputs& inputs, Core::Alloc::ScratchArena& scratchArena, SoftShadowSplitResourceUses& uses, SoftShadowSplitTasks& tasks);


private:
    // Adds the map, cap, and CSG bound reads to the opaque producer and transparent trace.
    [[nodiscard]] Expected<LightSpaceShadowMaps> declareLightSpaceShadowMaps(
        const SoftShadowSplitTaskChainInputs& inputs,
        Core::Alloc::ScratchArena& scratchArena,
        SoftShadowSplitResourceUses& uses
    );
    [[nodiscard]] b8 declareOpaqueTasks(
        const SoftShadowSplitTaskChainInputs& inputs,
        const SoftShadowSplitResourceUses& uses,
        const Core::GpuTaskId& shadowTraceDependency,
        const Core::GpuTaskSchedulingHint& tailScheduling,
        SoftShadowSplitTasks& tasks
    );
    [[nodiscard]] b8 declareTransparentTasks(
        const SoftShadowSplitTaskChainInputs& inputs,
        const SoftShadowSplitResourceUses& uses,
        const Core::GpuTaskSchedulingHint& tailScheduling,
        Core::GpuGraphResourceId csgShadowContext,
        Core::Alloc::ScratchArena& scratchArena,
        SoftShadowSplitTasks& tasks
    );


private:
    Core::GpuTaskGraph& m_graph;
    Core::GraphicsRuntime& m_graphics;
    Core::Alloc::GlobalArena& m_arena;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;
struct DeferredFrameTargets;
struct RayTracingShadowPreparationResourceSnapshot;
struct RayTracingDeferredGraphResourceSnapshot;
struct RayTracingShadowVisibilityGraphPlanSnapshot;
struct RayTracingSceneGraphReads;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowVisibilityResourceInputs{
    const DeferredFrameTargets& deferredTargets;
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan;
    const RayTracingSceneGraphReads& hardwareSceneReads;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId shadowVisibility;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId materialContextSlots;
    Span<const Core::GpuGraphResourceId> traceGeometryResources;
    b8 traceGeometryStatesGraphOwned;
    b8 splitSoftTransparentFold;
    b8 graphOwnsOpaqueTemporalMergeEntryStates;
    b8 softShadowHistoryReadable;
    b8 opaqueHistoryFrontIsA;
    b8 hardwareTransparentTrace;
    b8 adaptiveCompact;
};

struct ShadowVisibilityResourcePlan{
    Core::GpuGraphResourceId sceneGeometryDomain;
    Core::GpuGraphResourceId sceneTlas;
    Array<Core::GpuGraphResourceId, s_HwTransparentScratchBufferCount> hardwareTransparentScratch{};
    Core::GpuGraphResourceId adaptiveEdgeCounter;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses;


public:
    explicit ShadowVisibilityResourcePlan(Core::Alloc::ScratchArena& scratchArena)noexcept
        : resourceUses(scratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowVisibilityResourcePlanner final : NoCopy{
public:
    ShadowVisibilityResourcePlanner(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] b8 prepare(const ShadowVisibilityResourceInputs& inputs, ShadowVisibilityResourcePlan& result);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingState;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowReprojectMergeHeapResources{
    Core::Texture* softTrace = nullptr;
    Core::Texture* historyIn = nullptr;
    Core::Texture* momentsIn = nullptr;
    Core::Texture* historyOut = nullptr;
    Core::Texture* momentsOut = nullptr;
    u32 softTraceSlot = 0u;
    u32 historyInSlot = 0u;
    u32 momentsInSlot = 0u;
    u32 historyOutStorageSlot = 0u;
    u32 momentsOutStorageSlot = 0u;
};


class SoftShadowTemporalMergeRecorder final : NoCopy{
public:
    SoftShadowTemporalMergeRecorder(
        const RendererRayTracingState& state, u32 slotRangeCount, u32 halfWidth, u32 halfHeight, u32 groupsX, u32 groupsY, b8 historyReadable,
        Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap
    )noexcept;


public:
    void record(const ShadowReprojectMergeHeapResources& resources, b8 graphOwnsSoftTraceInputState, b8 graphOwnsMergeCurrentGeometryEntryState, b8 graphOwnsMergeStaticReadEntryStates, b8 graphOwnsMergeTemporalEntryStates)const;


private:
    const RendererRayTracingState& m_state;
    Core::CommandList& m_commandList;
    DeferredFrameTargets& m_targets;
    Core::GpuDescriptorHeap& m_heap;
    u32 m_slotRangeCount;
    u32 m_halfWidth;
    u32 m_halfHeight;
    u32 m_groupsX;
    u32 m_groupsY;
    b8 m_historyReadable;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphOwnedAdaptiveShadowPlan;
class RendererRayTracingState;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SoftwareAdaptiveShadowRecorder final{
public:
    static void Record(const GraphOwnedAdaptiveShadowPlan* plan, const RendererRayTracingState& state, Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingState;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SoftwareTransparentShadowTraceRecorder final{
public:
    static void Record(const SoftwareTransparentShadowTraceInputs& inputs, const RendererRayTracingState& state, Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingShadowVisibilityTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both opaque resolve callbacks share one all-lit fallback: clear visibility, realign UAV state, retire timing.
template<typename PayloadT>
inline void RecoverOpaqueSoftShadowAllLit(const PayloadT& payload, Core::CommandList& commandList){
    payload.raytracingSystem.clearShadowVisibility(commandList, payload.targets);
    // Skipped transparent callbacks still declare this output as UAV. Realign the tracker after the typed fallback clear.
    commandList.setTextureState(payload.targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();
    payload.opaqueProduced = false;
    if(payload.asyncTiming.has_value()){
        payload.asyncTiming.value().discardTiming();
        payload.asyncTiming.reset();
    }
    if(payload.shadowVisibilityTiming.has_value()){
        payload.shadowVisibilityTiming.value().discardTiming();
        payload.shadowVisibilityTiming.reset();
    }
    payload.opaqueResolveTiming.value().discardTiming();
    payload.opaqueResolveTiming.reset();
}

template<typename PayloadT>
inline void DiscardOpaqueSoftShadowResolve(PayloadT& payload)noexcept{
    payload.opaqueProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
    Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
    Core::DiscardGpuTimingMeasure(&payload.opaqueResolveTiming);
}

template<typename PayloadT>
inline void DiscardTransparentSoftShadowResolve(PayloadT& payload)noexcept{
    payload.transparentTraceProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.transparentResolveTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A prepared frame records production, resolve, trace, fold in one packet. Shared state is stack-owned; never survives retry.
struct ShadowVisibilityOpaqueGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: opaque producer always records against this frame's ticket and brackets the shared async/shadow timing.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        b8 hardwareShadowSupported = false;
        b8 graphOwnsOpaqueTemporalMergeEntryStates = false;
        LightSpaceShadowSnapshot lightSpace;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
        const b8& prepared;
        b8& opaqueProduced;
        u32& opaqueFrameIndex;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
    static void Discarded(Payload& payload)noexcept;
};


// The opaque producer leaves the trace and current geometry scratch in their declared states.
// This adjacent task owns their sampled entry before temporal merge and, unless fused later, the first wavelet.
// Its tail retains dynamic ping-pong and upsample work, while the terminal transparent fold remains the output/acceptance owner.
struct ShadowVisibilityOpaqueFirstWaveletGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: first wavelet always brackets the opaque-resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
        Optional<Core::GpuTimingMeasure>& opaqueResolveTiming;
        b8& opaqueProduced;
        const u32& opaqueFrameIndex;
        b8 hardwareShadowSupported = false;
        b8 graphOwnsOpaqueTemporalMergeEntryStates = false;
        b8 deferUpsample = false;
        b8 deferWavelet = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        DiscardOpaqueSoftShadowResolve(payload);
    }
};


struct ShadowVisibilityOpaqueResolveTailGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: resolve tail always closes the opaque-resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
        Optional<Core::GpuTimingMeasure>& opaqueResolveTiming;
        b8& opaqueProduced;
        const u32& opaqueFrameIndex;
        b8 hardwareShadowSupported = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        DiscardOpaqueSoftShadowResolve(payload);
    }
};


// The prepared path keeps trace and resolve apart so the graph lowers transparent output from UAV to shader-read.
struct ShadowTransparentSoftTraceGraphTask{
    // Transparent hardware fallback also initializes its indirect dispatch arguments through a buffer upload.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, };


public:
    // All bindings are required: transparent trace always records against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        LightSpaceShadowSnapshot lightSpace;
        Core::GpuTimingSubmissionTicket& timingTicket;
        const b8& opaqueProduced;
        const u32& opaqueFrameIndex;
        b8& transparentTraceProduced;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        payload.transparentTraceProduced = false;
    }
};


// The transparent trace publishes half-res RGB visibility. On temporal frames it freezes the history pair, publishes the next pair, starts resolve.
struct ShadowTransparentSoftTemporalMergeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: temporal merge always brackets the transparent-resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming;
        const b8& opaqueProduced;
        b8& transparentTraceProduced;
        const u32& opaqueFrameIndex;
        b8 graphOwnsTransparentTemporalMergeEntryStates = false;
        b8 combinedTemporal = false;
        b8 hardwareShadowSupported = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        DiscardTransparentSoftShadowResolve(payload);
    }
};


// Trace or merge publishes the first wavelet input. The terminal fold keeps the final upsample plus output/acceptance.
struct ShadowTransparentSoftFirstWaveletGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: first wavelet always brackets the transparent-resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming;
        const b8& opaqueProduced;
        b8& transparentTraceProduced;
        const u32& opaqueFrameIndex;
        b8 graphOwnsTransparentWaveletInputBoundary = false;
        b8 startsTransparentResolveTiming = true;
        b8 combinedWavelet = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        DiscardTransparentSoftShadowResolve(payload);
    }
};


struct ShadowTransparentSoftFoldGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: terminal fold always closes the shared async/shadow/resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming;
        const b8& opaqueProduced;
        b8& transparentTraceProduced;
        const u32& opaqueFrameIndex;
        b8 combinedUpsample = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
    static void Discarded(Payload& payload)noexcept;
};


// Shadow visibility owns its graph task; pipeline composes the caustics successor.
struct ShadowVisibilityGraphTask{
    // Transparent hardware fallback also initializes its indirect dispatch arguments through a buffer upload.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, };


public:
    // All bindings are required: visibility always records against this frame's bindings.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        const b8& prepared;
        GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan;
        b8 hardwareShadowSupported = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        payload.raytracingSystem.discardSoftShadowTemporalHistory();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u32> LightSpaceShadowDrawCount(const Span<const LightSpaceShadowCaster> casters)noexcept{
    u64 drawCount = 0u;
    for(const LightSpaceShadowCaster& caster : casters){
        drawCount += LightSpaceCasterDrawCount(caster.meshletCount);
        if(drawCount > Limit<u32>::s_Max)
            return MakeUnexpected(Failure{});
    }
    return static_cast<u32>(drawCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::setSoftwareShadowSettings(const SoftwareShadowSettings& settings)noexcept{
    if(!ValidateSoftwareShadowSettings(settings))
        return false;
    auto& state = m_lightSpaceShadow;
    if(SameSoftwareShadowSettings(state.m_settings, settings))
        return true;
    ApplySoftwareShadowSettingsChange(
        settings,
        state.m_settings,
        state.m_captureHistory,
        state.m_captureReuseLogged,
        state.m_snapshot.ready,
        state.m_resourcesPrepared,
        state.m_dispatchLogged,
        m_rayTracingState.m_softShadowTemporalSeeded
    );
    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    return true;
}

LightSpaceShadowSnapshot RendererRayTracingSystem::lightSpaceShadowSnapshot()const noexcept{
    LightSpaceShadowSnapshot result = m_lightSpaceShadow.m_snapshot;
    result.casters = m_lightSpaceShadow.m_casters;
    result.csgContextBytes = m_lightSpaceShadow.m_csg.bytes;
    result.csgDynamicBounds = m_lightSpaceShadow.m_csg.dynamicBounds;
    return result;
}

void RendererRayTracingSystem::acceptLightSpaceShadowCapture(const LightSpaceCaptureTicket& ticket, const b8 prepared){
    auto& state = m_lightSpaceShadow;
    if(!prepared || !state.m_captureHistory.accept(ticket))
        return;
    LogAcceptedLightSpaceCaptureReuseIfNeeded(
        ticket.reuse,
        state.m_captureHistory.acceptedReuseAge(),
        state.m_settings.captureCadence,
        state.m_captureReuseLogged
    );
}

void RendererRayTracingSystem::preflightLightSpaceShadowResources(){
    auto& state = m_lightSpaceShadow;
    auto& diagnostics = state.m_diagnostics;
    if(state.m_csg.snapshot.hasCsg || diagnostics.submission.valid()){
        ++diagnostics.frameIndex;
        const Core::QueueSubmissionToken submission = diagnostics.submission;
        if(submission.valid() && submission.hasPhysicalQueueIdentity()){
            auto& device = m_graphics.getDevice();
            const Core::GpuPhysicalQueueId queue{ submission.physicalQueueIndex, submission.deviceGeneration };
            const Expected<u64> completedValue = device.queueGetCompletedInstance(queue);
            if(completedValue && *completedValue >= submission.value){
                const Expected<void*> mappedFailures = device.mapBuffer(*diagnostics.readback, Core::CpuAccessMode::Read);
                if(mappedFailures){
                    const u32 failureFlags = *MakeNotNull(static_cast<const u32*>(*mappedFailures));
                    device.unmapBuffer(*diagnostics.readback);
                    LightSpaceShadowLogReporter{}.reportRayFailures(failureFlags, diagnostics.reportedFailures);
                    diagnostics.submission = {};
                }
            }
        }
    }
    state.m_resourcesPrepared = false;
    state.m_csgRequired = false;
    if(!state.m_csg.snapshot.hasCsg && (m_shadowVisibilityHardwareSupported || state.m_settings.backend == SoftwareShadowBackend::SoftwareTrace || !state.m_sceneEligible))
        return;
    // Use the same scene ordering and shadow-slot allocator as the later prefix. That prefix may only select a fitting generation.
    Array<SceneLightGpuData, NWB_SCENE_MAX_LIGHTS> lights{};
    Array<f32, NWB_SCENE_MAX_LIGHTS> causticImportance{};
    const u32 lightCount = ECSRenderDetail::ResolveSceneLights(m_world, lights, causticImportance);
    const Span<const SceneLightGpuData> activeLights(lights.data(), lightCount);
    state.m_csgRequired = HasAnyLightSpaceCsgActiveLight(state.m_csg.snapshot.hasCsg, activeLights);
    const auto planResult = buildLightSpaceShadowPlan(activeLights);
    if(!planResult)
        return;
    const LightSpacePlan& plan = *planResult;
    LightSpaceResourceSet resources(m_arena, m_graphics, m_shaderSystem, m_lightSpaceShadow, m_shadowVisibilityHardwareSupported);
    state.m_resourcesPrepared = resources.ensureLightSpaceShadowPipelines() && resources.ensureLightSpaceShadowStorage(plan);
}

Expected<LightSpacePlan> RendererRayTracingSystem::buildLightSpaceShadowPlan(const Span<const SceneLightGpuData> lights)const noexcept{
    const auto& state = m_lightSpaceShadow;
    const usize lightCount = lights.size();
    if(
        (!state.m_csg.snapshot.hasCsg && (m_shadowVisibilityHardwareSupported || state.m_settings.backend == SoftwareShadowBackend::SoftwareTrace))
        || !state.m_sceneEligible || !m_shadowVisibilityPreparedTargets || (!m_shadowVisibilityHardwareSupported && !m_preparedSceneSwBvhTraversal.ready())
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || state.m_casters.empty() || lightCount > NWB_SCENE_MAX_LIGHTS
    )
        return MakeUnexpected(Failure{});
    const auto drawCount = LightSpaceShadowDrawCount(state.m_casters);
    if(!drawCount)
        return MakeUnexpected(drawCount.error());
    return TryBuildLightSpacePlanForSceneLights(
        state.m_settings,
        lights,
        m_graphics.getDevice().getMaxStorageBufferRange(),
        *drawCount,
        state.m_csg.snapshot.hasCsg,
        state.m_csg.bytes.size()
    );
}

// Graph declaration can change the admitted light list, but never creates or grows its GPU resources.
void RendererRayTracingSystem::prepareLightSpaceShadows(const Span<const SceneLightGpuData> lights){
    auto& state = m_lightSpaceShadow;
    auto& snapshot = state.m_snapshot;
    snapshot.ready = false;
    snapshot.diagnostics = nullptr;
    snapshot.diagnosticReadback = nullptr;
    if(
        !state.m_resourcesPrepared || !snapshot.layout || !snapshot.viewPipeline || !snapshot.cullPipeline || !snapshot.opaqueResolve
        || !snapshot.transparentResolve || !snapshot.opaqueFallback || !snapshot.transparentFallback || !snapshot.shadePipeline
        || !snapshot.opaqueCapture || !snapshot.transparentCapture
        || !snapshot.counts || !snapshot.events || !snapshot.views || !snapshot.drawArguments || !snapshot.depth
    )
        return;
    const auto planResult = buildLightSpaceShadowPlan(lights);
    if(!planResult)
        return;
    const LightSpacePlan& plan = *planResult;
    const auto drawCount = LightSpaceShadowDrawCount(state.m_casters);
    NWB_ASSERT(drawCount);
    const LightSpaceShadowStorageCapacity capacity{
        snapshot.counts->getCreationDescription().byteSize, snapshot.events->getCreationDescription().byteSize,
        snapshot.views->getCreationDescription().byteSize, snapshot.drawArguments->getCreationDescription().byteSize,
        snapshot.depth->getCreationDescription().width,
        snapshot.depth->getCreationDescription().arraySize,
    };
    if(!LightSpaceShadowStorageFits(plan, capacity))
        return;
    for(u32 view = 0u; view < plan.viewCount; ++view){
        if(!snapshot.opaqueFramebuffers[view] || !snapshot.transparentFramebuffers[view])
            return;
    }
    DeferredFrameTargets& targets = *m_shadowVisibilityPreparedTargets;
    const LightSpaceCaptureIdentity identity = LightSpaceCaptureIdentityBuilder().buildCaptureIdentity(
        state.m_captureSceneIdentity,
        state.m_captureSceneTrusted,
        lights,
        targets.width,
        targets.height,
        plan
    );
    snapshot.captureTicket = state.m_captureHistory.prepare(identity, state.m_settings.captureCadence);
    snapshot.captureHistory = &state.m_captureHistory;
    // Reuse retains the fitted views, CSG context and converted crossings together; current templates must not overwrite them.
    if(!snapshot.captureTicket.reuse){
        snapshot.plan = plan;
        // Keep hashed resource objects alive until their map generation is replaced; pointer identities cannot alias replacements.
        state.m_captureBuffers.assign(state.m_sceneBuffers.begin(), state.m_sceneBuffers.end());
        state.m_captureTextures.assign(state.m_sceneTextures.begin(), state.m_sceneTextures.end());
    }
    b8 hasOrdinaryTransparent = false;
    if(state.m_csg.snapshot.hasCsg){
        for(const LightSpaceShadowCaster& caster : state.m_casters)
            hasOrdinaryTransparent = hasOrdinaryTransparent || (caster.transparent && !caster.csg);
    }
    snapshot.csgTlasDescriptor = m_shadowVisibilityHardwareSupported ? m_rayTracingState.m_tlasHeapHandle : Core::GpuDescriptorHandle::Invalid();
    PopulatePreparedLightSpaceShadowPush(
        snapshot.viewsDescriptor.slot(),
        m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        snapshot.countsDescriptor.slot(),
        snapshot.eventsDescriptor.slot(),
        snapshot.depthDescriptor.slot(),
        static_cast<u32>(state.m_casters.size()),
        *drawCount,
        targets.width,
        targets.shadowReceiverFactor,
        targets.height,
        targets.bindless.slotsBufferDescriptor.slot(),
        m_shadowVisibilityHardwareSupported ? 0u : m_preparedSceneBvh.nodeHeapHandle().slot(),
        state.m_csg.snapshot.hasCsg,
        m_shadowVisibilityHardwareSupported,
        hasOrdinaryTransparent,
        snapshot.csgContextDescriptor.slot(),
        snapshot.csgOpaqueDepthDescriptor.slot(),
        plan.viewCount,
        snapshot.drawArgumentsDescriptor.slot(),
        snapshot.push
    );
    auto& diagnostics = state.m_diagnostics;
    snapshot.diagnostics = state.m_csg.snapshot.hasCsg ? &diagnostics : nullptr;
    if(state.m_csg.snapshot.hasCsg && diagnostics.readback && !diagnostics.submission.valid() && IsLightSpaceDiagnosticIntervalDue(diagnostics.frameIndex, diagnostics.acceptedFrame)){
        snapshot.diagnosticReadback = diagnostics.readback;
        snapshot.diagnosticFrame = diagnostics.frameIndex;
    }
    snapshot.casters = state.m_casters;
    snapshot.ready = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 BuildLightSpaceCsgCaptureIdentity(
    const LightSpaceCsgState& state,
    const Span<const NwbRtInstanceMaterialGpu> materials, const Span<const InstanceGpuData> instances,
    const Span<const u8> materialBytes,
    const Span<const Core::TextureHandle> textures
)noexcept{
    NWB_ASSERT(materials.size() == instances.size());
    u64 identity = LightSpaceCaptureIdentityBuilder().buildCsgBaseIdentity(
        state.captureGeometryIdentity,
        state.snapshot.contentIdentity,
        materials,
        instances,
        materialBytes,
        textures.size()
    );
    for(const Core::TextureHandle& texture : textures)
        Fnv64AppendValue(identity, texture.get());
    return identity;
}

void BeginLightSpaceCsgGather(LightSpaceCsgState& state, Core::ECS::World& world, const usize capacity, const b8 hardware){
    state.gathering = HasCsgFrameCandidates(world);
    state.hardware = hardware;
    state.snapshot.hasCsg = false;
    state.snapshot.identity = 0u;
    state.snapshot.contentIdentity = 0u;
    state.captureGeometryIdentity = s_Fnv64OffsetBasis;
    state.captureGeometryTrusted = true;
    state.snapshot.receiverRanges.clear();
    state.snapshot.cutters.clear();
    state.receivers.clear();
    state.instances.clear();
    state.dynamicBounds.clear();
    state.bytes.clear();
    state.solidTriangleMasks.clear();
    if(!state.gathering)
        return;
    state.receivers.reserve(capacity);
    state.instances.reserve(capacity);
    state.dynamicBounds.reserve(capacity);
    state.solidTriangleMasks.reserve(capacity);
}

void AppendLightSpaceCsgReceiver(
    LightSpaceCsgState& state,
    const Core::ECS::EntityID entity,
    const b8 transparent,
    const SIMDMatrix& objectToWorld,
    const SIMDMatrix& worldToObject,
    const SIMDVectorSingle localMin,
    const SIMDVectorSingle localMax,
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh
){
    if(!state.gathering)
        return;
    LightSpaceCaptureIdentityBuilder().updateReceiverGeometry(
        entity,
        transparent,
        mesh.meshName,
        reinterpret_cast<u64>(mesh.positionBuffer.get()),
        reinterpret_cast<u64>(mesh.triangleIndexBuffer.get()),
        reinterpret_cast<u64>(mesh.attributeBuffer.get()),
        mesh.meshletCount,
        reinterpret_cast<u64>(mesh.meshletDescBuffer.get()),
        reinterpret_cast<u64>(mesh.meshletLocalBoundsBuffer.get()),
        mesh.meshletDescHeapHandle.value,
        mesh.meshletLocalBoundsHeapHandle.value,
        reinterpret_cast<u64>(mesh.runtimeLocalBoundsBuffer.get()),
        mesh.runtimeLocalBoundsHeapHandle.value,
        reinterpret_cast<u64>(mesh.swBvhNodeBuffer.get()),
        mesh.swBvhNodeHeapHandle.value,
        mesh.meshletPrimitiveIndexCount,
        mesh.runtimeMeshVersion,
        mesh.runtimeGeometryContentRevision,
        mesh.runtimeMesh,
        state.captureGeometryIdentity,
        state.captureGeometryTrusted
    );

    CsgShadowReceiverInput receiver;
    receiver.entity = entity;
    receiver.receiverPass = transparent ? CsgReceiverPass::Transparent : CsgReceiverPass::Opaque;
    SIMDVectorSingle worldMin{};
    SIMDVectorSingle worldMax{};
    receiver.boundsValid = false;
    if(CsgReceiverBoundsCanCull(mesh.csgLocalBounds) && !mesh.runtimeMesh){
        const auto worldBounds = AabbTests::Transform(objectToWorld, localMin, localMax);
        if(worldBounds){
            worldMin = worldBounds->minBounds;
            worldMax = worldBounds->maxBounds;
            receiver.boundsValid = true;
        }
    }
    StoreFloat(worldMin, receiver.worldMin);
    StoreFloat(worldMax, receiver.worldMax);
    state.receivers.push_back(receiver);

    const b8 hasRuntimeBounds = mesh.runtimeLocalBoundsBuffer && mesh.runtimeLocalBoundsHeapHandle.valid();
    if(hasRuntimeBounds)
        state.dynamicBounds.push_back(mesh.runtimeLocalBoundsBuffer);
    state.instances.push_back(BuildLightSpaceCsgInstanceGpu(
        worldToObject,
        localMin,
        localMax,
        mesh.meshletPrimitiveIndexCount,
        hasRuntimeBounds,
        hasRuntimeBounds ? mesh.runtimeLocalBoundsHeapHandle.slot() : s_LightSpaceInvalidGpuDescriptorSlot,
        state.hardware,
        mesh.swBvhNodeHeapHandle.valid(),
        mesh.swBvhNodeHeapHandle.valid() ? mesh.swBvhNodeHeapHandle.slot() : s_LightSpaceInvalidGpuDescriptorSlot
    ));
    state.solidTriangleMasks.push_back(mesh.solidTriangleWords);
}

b8 FinishLightSpaceCsgGather(const CsgShapeRegistry& registry, LightSpaceCsgState& state, Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena){
    if(!state.gathering)
        return true;
    if(state.receivers.size() != state.instances.size())
        return false;
    if(!BuildCsgShadowSnapshot(registry, state.receivers, world, scratchArena, state.snapshot))
        return false;
    if(!state.snapshot.hasCsg)
        return true;
    static_assert(sizeof(LightSpaceCsgContextHeader) == NWB_CSG_RAY_CONTEXT_BYTES);
    static_assert(sizeof(u32) == NWB_CSG_RAY_MEMBERSHIP_WORD_BYTES);
    if(!LightSpaceCsgContextPacker().pack(
        Span<const CsgShadowReceiverRangeGpu>(state.snapshot.receiverRanges.data(), state.snapshot.receiverRanges.size()),
        Span<const CsgCutterGpuData>(state.snapshot.cutters.data(), state.snapshot.cutters.size()),
        Span<const LightSpaceCsgInstanceGpu>(state.instances.data(), state.instances.size()),
        Span<const Span<const u32>>(state.solidTriangleMasks.data(), state.solidTriangleMasks.size()),
        state.bytes
    ))
        return false;
    LightSpaceCaptureIdentityBuilder().updateCsgInstances(
        Span<const LightSpaceCsgInstanceGpu>(state.instances.data(), state.instances.size()),
        state.snapshot.identity
    );
    LightSpaceShadowLogReporter{}.reportUnsupportedLimitations(
        Span<const CsgShadowReceiverRangeGpu>(state.snapshot.receiverRanges.data(), state.snapshot.receiverRanges.size()),
        state.limitationLogged
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RecordLightSpaceViews(const LightSpaceShadowSnapshot& snapshot, Core::CommandList& commandList, Core::GpuDescriptorHeap& heap){
    if(!snapshot.ready || !snapshot.viewPipeline || !snapshot.drawArguments || snapshot.plan.viewCount == 0u)
        return false;
    commandList.setComputeState(Core::ComputeState{}.setPipeline(snapshot.viewPipeline.get()));
    heap.bindCompute(commandList, *snapshot.viewPipeline);
    commandList.setPushConstants(&snapshot.push, sizeof(snapshot.push));
    commandList.dispatch(DivideUp(snapshot.plan.viewCount, static_cast<u32>(NWB_LIGHT_SPACE_VIEW_GROUP_SIZE)), 1u, 1u);
    return true;
}

b8 RecordLightSpaceCull(const LightSpaceShadowSnapshot& snapshot, Core::CommandList& commandList, Core::GpuDescriptorHeap& heap){
    if(!snapshot.ready || !snapshot.cullPipeline || !snapshot.drawArguments || snapshot.casters.empty() || snapshot.plan.viewCount == 0u)
        return false;
    commandList.setComputeState(Core::ComputeState{}.setPipeline(snapshot.cullPipeline.get()));
    heap.bindCompute(commandList, *snapshot.cullPipeline);
    LightSpaceShadowPush push = snapshot.push;
    push.casterDrawOffset = 0u;
    for(const auto& caster : snapshot.casters){
        const auto drawCount = LightSpaceShadowDrawCount(Span<const LightSpaceShadowCaster>(&caster, 1u));
        if(!drawCount)
            return false;
        push.instanceIndex = caster.instanceIndex;
        push.meshletDescSlot = caster.meshletDescSlot;
        push.meshletBoundsSlot = caster.meshletBoundsSlot;
        push.meshletCount = caster.meshletCount;
        commandList.setPushConstants(&push, sizeof(push));
        const u32 groups = DivideUp(*drawCount, static_cast<u32>(NWB_LIGHT_SPACE_CULL_GROUP_SIZE));
        commandList.dispatch(Min(groups, static_cast<u32>(NWB_LIGHT_SPACE_CULL_MAX_GROUPS_X)), snapshot.plan.viewCount, DivideUp(groups, static_cast<u32>(NWB_LIGHT_SPACE_CULL_MAX_GROUPS_X)));
        push.casterDrawOffset += *drawCount;
    }
    return true;
}

b8 RecordLightSpaceCapture(const LightSpaceShadowSnapshot& snapshot, const u32 viewIndex, const b8 transparent, Core::CommandList& commandList, Core::GpuDescriptorHeap& heap){
    static_assert(sizeof(Core::DrawIndexedIndirectArguments) == NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES);
    if(!snapshot.ready || !snapshot.drawArguments || viewIndex >= snapshot.plan.viewCount)
        return false;
    const auto& framebuffer = transparent ? snapshot.transparentFramebuffers[viewIndex] : snapshot.opaqueFramebuffers[viewIndex];
    const auto& pipeline = transparent ? snapshot.transparentCapture : snapshot.opaqueCapture;
    if(!framebuffer || !pipeline)
        return false;
    for(const auto& caster : snapshot.casters){
        if(!LightSpaceCasterEligibleForCapturePass(caster.transparent, caster.csg, transparent, caster.indexCount, snapshot.push.csgFlags))
            continue;
        if(!caster.triangleIndexBuffer || !caster.triangleIndexBuffer->getCreationDescription().isIndexBuffer)
            return false;
    }
    Core::RenderPassParameters parameters;
    parameters.depthClearValue = 1.f;
    parameters.depthAttachmentActions.loadAction = transparent ? Core::RenderPassLoadAction::Load : Core::RenderPassLoadAction::Clear;
    commandList.beginRenderPass(*framebuffer, parameters);
    const u32 resolution = snapshot.plan.views[viewIndex].map[0];
    Core::ViewportState viewport;
    viewport.addViewport(Core::Viewport(static_cast<f32>(resolution), static_cast<f32>(resolution)));
    viewport.addScissorRect(Core::Rect(0, static_cast<i32>(resolution), 0, static_cast<i32>(resolution)));
    Core::GraphicsState graphics;
    graphics.setPipeline(pipeline.get()).setFramebuffer(framebuffer.get()).setViewport(viewport).setIndirectParams(snapshot.drawArguments.get());

    LightSpaceShadowPush push = snapshot.push;
    push.viewIndex = viewIndex;
    u32 drawOffset = viewIndex * snapshot.push.captureDrawCount;
    for(const LightSpaceShadowCaster& caster : snapshot.casters){
        const auto drawCount = LightSpaceShadowDrawCount(Span<const LightSpaceShadowCaster>(&caster, 1u));
        if(!drawCount){
            commandList.endRenderPass();
            return false;
        }
        const u32 argumentOffset = drawOffset * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES;
        drawOffset += *drawCount;
        if(!LightSpaceCasterEligibleForCapturePass(caster.transparent, caster.csg, transparent, caster.indexCount, snapshot.push.csgFlags))
            continue;
        graphics.setIndexBuffer(Core::IndexBufferBinding{}.setBuffer(caster.triangleIndexBuffer.get()).setFormat(Core::Format::R32_UINT));
        commandList.setGraphicsState(graphics);
        heap.bindGraphics(commandList, *pipeline);
        push.instanceIndex = caster.instanceIndex;
        commandList.setPushConstants(&push, sizeof(push));
        const u32 captureCount = (snapshot.plan.views[viewIndex].light[3] & NWB_LIGHT_SPACE_FLAG_POINT) != 0u ? *drawCount : 1u;
        for(u32 firstDraw = 0u; firstDraw < captureCount;){
            const u32 batchCount = Min(captureCount - firstDraw, s_LightSpaceMaxIndirectDraws);
            commandList.drawIndexedIndirect(argumentOffset + firstDraw * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES, batchCount);
            firstDraw += batchCount;
        }
    }
    commandList.endRenderPass();
    return true;
}

b8 RecordLightSpaceShade(const LightSpaceShadowSnapshot& snapshot, Core::CommandList& commandList, Core::GpuDescriptorHeap& heap){
    const auto& pipeline = (snapshot.push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u ? snapshot.csgShadePipeline : snapshot.shadePipeline;
    if(!snapshot.ready || !pipeline || snapshot.plan.totalPixels == 0u)
        return false;
    LightSpaceShadowPush push = snapshot.push;
    push.width = snapshot.plan.totalPixels;
    commandList.setComputeState(Core::ComputeState{}.setPipeline(pipeline.get()));
    heap.bindCompute(commandList, *pipeline);
    commandList.setPushConstants(&push, sizeof(push));
    const u32 groups = DivideUp(push.width, static_cast<u32>(NWB_LIGHT_SPACE_SHADE_GROUP_SIZE));
    const u32 groupsX = Min(groups, static_cast<u32>(NWB_LIGHT_SPACE_SHADE_MAX_GROUPS_X));
    const u32 groupsY = DivideUp(groups, static_cast<u32>(NWB_LIGHT_SPACE_SHADE_MAX_GROUPS_X));
    commandList.dispatch(groupsX, groupsY, 1u);
    return true;
}

b8 RecordLightSpaceResolve(
    const LightSpaceShadowSnapshot& snapshot,
    const u32 frameIndex, const u32 sampleCount, const u32 outputSlot, const b8 transparent,
    Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, Core::GpuTimingRecorder& timing, Core::Texture& outputTexture
){
    const b8 csg = (snapshot.push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u;
    const b8 hardwareCsg = (snapshot.push.csgFlags & NWB_CSG_SHADOW_FLAG_HW_COMPOSE) != 0u;
    const auto& pipeline = csg ? (transparent ? snapshot.csgTransparentResolve : snapshot.csgOpaqueResolve) : (transparent ? snapshot.transparentResolve : snapshot.opaqueResolve);
    const auto& fallback = csg ? (transparent ? snapshot.csgTransparentFallback : snapshot.csgOpaqueFallback) : (transparent ? snapshot.transparentFallback : snapshot.opaqueFallback);
    if(!snapshot.ready || !pipeline || sampleCount == 0u || (hardwareCsg ? !snapshot.csgTlasDescriptor.valid() : !fallback))
        return false;
    LightSpaceShadowPush push = snapshot.push;
    push.frameIndex = frameIndex;
    push.sampleCount = sampleCount;
    push.outputSlot = outputSlot;
    commandList.setComputeState(Core::ComputeState{}.setPipeline(pipeline.get()));
    if(hardwareCsg)
        heap.bindCompute(commandList, *pipeline, snapshot.csgTlasDescriptor);
    else
        heap.bindCompute(commandList, *pipeline);
    commandList.setPushConstants(&push, sizeof(push));
    const u32 halfWidth = DivideUp(push.width, push.receiverFactor);
    const u32 halfHeight = DivideUp(push.height, push.receiverFactor);
    const u32 groupsX = DivideUp(halfWidth, static_cast<u32>(NWB_LIGHT_SPACE_GROUP_SIZE));
    const u32 groupsY = DivideUp(halfHeight, static_cast<u32>(NWB_LIGHT_SPACE_GROUP_SIZE));
    {
        const auto& scope = transparent ? RendererGpuTimingScope::s_LightSpaceShadowMapTransparent : RendererGpuTimingScope::s_LightSpaceShadowMapOpaque;
        Core::GpuTimingMeasure measure(timing, scope, commandList.getDevice(), commandList);

        commandList.dispatch(groupsX, groupsY, 1u);
    }
    if(hardwareCsg)
        return true;
    // Alpha is initialized by the map dispatch before the fallback reads it through the same UAV binding.
    commandList.setTextureState(&outputTexture, Core::TextureSubresourceSet{}, Core::ResourceStates::UnorderedAccess, true);
    commandList.commitBarriers();
    commandList.setComputeState(Core::ComputeState{}.setPipeline(fallback.get()));
    heap.bindCompute(commandList, *fallback);
    commandList.setPushConstants(&push, sizeof(push));
    {
        const auto& scope = transparent ? RendererGpuTimingScope::s_LightSpaceShadowFallbackTransparent : RendererGpuTimingScope::s_LightSpaceShadowFallbackOpaque;
        Core::GpuTimingMeasure measure(timing, scope, commandList.getDevice(), commandList);

        commandList.dispatch(groupsX, groupsY, 1u);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 LightSpaceResourceSet::ensureLightSpaceShadowPipelines(){
    auto& state = m_state;
    auto& snapshot = state.m_snapshot;
    const b8 csg = state.m_csg.snapshot.hasCsg;
    const b8 hardwareCsg = csg && m_hardwareSupported;
    if(state.m_pipelineFailed)
        return false;
    if(
        snapshot.viewPipeline && snapshot.cullPipeline && snapshot.opaqueResolve && snapshot.transparentResolve
        && snapshot.opaqueFallback && snapshot.transparentFallback && snapshot.shadePipeline
        && snapshot.opaqueCapture && snapshot.transparentCapture
        && (!csg || (snapshot.csgShadePipeline && snapshot.csgOpaqueResolve && snapshot.csgTransparentResolve && (hardwareCsg || (snapshot.csgOpaqueFallback && snapshot.csgTransparentFallback))))
    )
        return true;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    // Fragment stores/atomics are mandatory baseline device features, independently of ray-query support.
    constexpr auto s_DepthSupport = Core::FormatSupport::Texture | Core::FormatSupport::DepthStencil | Core::FormatSupport::ShaderLoad;
    if(!heap.isInitialized() || (hardwareCsg && !heap.hasAccelStructLayout()) || (device.queryFormatSupport(Core::Format::D32) & s_DepthSupport) != s_DepthSupport)
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(LightSpaceShadowPush), Core::ShaderType::All, snapshot.layout))
        return false;
    using ComputeShaderMember = const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::*;
    const ComputeShaderMember csgResolveMember = hardwareCsg ? &ShadowSubsystem::lightSpaceResolveHwCs : &ShadowSubsystem::lightSpaceResolveCs;
    const Array<ComputeShaderMember, 12u> computeMembers = {
        &ShadowSubsystem::lightSpaceViewCs,
        &ShadowSubsystem::lightSpaceResolveCs,
        &ShadowSubsystem::lightSpaceResolveCs,
        &ShadowSubsystem::lightSpaceFallbackCs,
        &ShadowSubsystem::lightSpaceFallbackCs,
        &ShadowSubsystem::lightSpaceShadeCs,
        &ShadowSubsystem::lightSpaceShadeCs,
        csgResolveMember,
        csgResolveMember,
        &ShadowSubsystem::lightSpaceFallbackCs,
        &ShadowSubsystem::lightSpaceFallbackCs,
        &ShadowSubsystem::lightSpaceCullCs
    };
    const Array<AStringView, 12u> computeVariants = {
        Core::ShaderArchive::s_DefaultVariant,
        s_LightSpaceNoCsgOccluder0,
        s_LightSpaceNoCsgOccluder1,
        s_LightSpaceNoCsgOccluder0,
        s_LightSpaceNoCsgOccluder1,
        s_LightSpaceCsgShade0,
        s_LightSpaceCsgShade1,
        s_LightSpaceCsgOccluder0,
        s_LightSpaceCsgOccluder1,
        s_LightSpaceCsgOccluder0,
        s_LightSpaceCsgOccluder1,
        Core::ShaderArchive::s_DefaultVariant
    };
    const Array<NotNull<Core::ComputePipelineHandle*>, 12u> outputs = { MakeNotNull(&snapshot.viewPipeline), MakeNotNull(&snapshot.opaqueResolve), MakeNotNull(&snapshot.transparentResolve),
        MakeNotNull(&snapshot.opaqueFallback), MakeNotNull(&snapshot.transparentFallback), MakeNotNull(&snapshot.shadePipeline), MakeNotNull(&snapshot.csgShadePipeline),
        MakeNotNull(&snapshot.csgOpaqueResolve), MakeNotNull(&snapshot.csgTransparentResolve), MakeNotNull(&snapshot.csgOpaqueFallback), MakeNotNull(&snapshot.csgTransparentFallback), MakeNotNull(&snapshot.cullPipeline) };
    static_assert(computeMembers.size() == outputs.size());
    static_assert(computeVariants.size() == outputs.size());
    for(u32 index = 0u; index < outputs.size(); ++index){
        if(ShouldSkipLightSpacePipeline(index, csg, hardwareCsg))
            continue;
        if(!state.m_shaders[index] && !m_shaderSystem.loadShader<ComputeShader>(computeMembers[index], computeVariants[index], Name(s_LightSpaceShaderScope), state.m_shaders[index])){
            state.m_pipelineFailed = true;
            return false;
        }
    }
    Core::ShaderHandle& captureVs = state.m_shaders[s_LightSpaceCaptureVertexShaderIndex];
    Core::ShaderHandle& capturePs = state.m_shaders[s_LightSpaceCapturePixelShaderIndex];
    if(
        (!captureVs && !m_shaderSystem.loadShader<VertexShader>(&ShadowSubsystem::lightSpaceCaptureVs, Core::ShaderArchive::s_DefaultVariant, Name(s_LightSpaceShaderScope), captureVs))
        || (!capturePs && !m_shaderSystem.loadShader<PixelShader>(&ShadowSubsystem::lightSpaceCapturePs, Core::ShaderArchive::s_DefaultVariant, Name(s_LightSpaceShaderScope), capturePs))
    ){
        state.m_pipelineFailed = true;
        return false;
    }
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        if(ShouldSkipLightSpacePipeline(index, csg, hardwareCsg))
            continue;
        if(*outputs[index])
            continue;
        const b8 includeAccelStruct = hardwareCsg && index >= s_LightSpaceFirstCsgResolvePipelineIndex && index <= s_LightSpaceLastCsgResolvePipelineIndex;
        if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, state.m_shaders[index], snapshot.layout, includeAccelStruct, *outputs[index]))
            return false;
    }
    for(u32 transparent = 0u; transparent < s_LightSpaceCapturePipelineKindCount; ++transparent){
        auto& output = transparent != 0u ? snapshot.transparentCapture : snapshot.opaqueCapture;
        if(output)
            continue;
        Core::RasterState raster;
        raster.setCullMode(Core::RasterCullMode::None).enableDepthClip();
        Core::DepthStencilState depth;
        depth.setDepthTestEnable(transparent == 0u).setDepthWriteEnable(transparent == 0u).setDepthFunc(Core::ComparisonFunc::Less);
        Core::RenderState render;
        render.setRasterState(raster).setDepthStencilState(depth);
        Core::GraphicsPipelineDesc desc;
        desc.setVertexShader(state.m_shaders[s_LightSpaceCaptureVertexShaderIndex]).setRenderState(render).addBindingLayout(snapshot.layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        if(transparent != 0u)
            desc.setPixelShader(state.m_shaders[s_LightSpaceCapturePixelShaderIndex]);
        auto outputResult = device.createGraphicsPipeline(desc, Core::FramebufferInfo{}.setDepthFormat(Core::Format::D32));
        if(outputResult)
            output = Move(*outputResult);
        else
            output.reset();
        if(!outputResult)
            return false;
    }
    return true;
}

b8 LightSpaceResourceSet::ensureLightSpaceShadowStorage(const LightSpacePlan& plan){
    auto& snapshot = m_state.m_snapshot;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const Array<u64, 4u> sizes = { plan.countByteSize, plan.eventByteSize, plan.viewByteSize, plan.drawArgumentByteSize };
    const u64 maximumRange = device.getMaxStorageBufferRange();
    NWB_ASSERT(
        sizes[0] != 0u && sizes[0] <= maximumRange && sizes[0] <= Limit<u32>::s_Max
        && sizes[1] != 0u && sizes[1] <= maximumRange && sizes[1] <= Limit<u32>::s_Max
        && sizes[2] != 0u && sizes[2] <= maximumRange && sizes[2] <= Limit<u32>::s_Max
        && sizes[3] != 0u && sizes[3] <= maximumRange && sizes[3] <= Limit<u32>::s_Max
    );
    if(m_state.m_csg.snapshot.hasCsg){
        auto& diagnostics = m_state.m_diagnostics;
        if(!diagnostics.readback){
            Core::BufferDesc desc;
            desc
                .setByteSize(sizeof(u32)).setCpuAccess(Core::CpuAccessMode::Read)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setDebugName(Name(s_LightSpaceResourceCsgDiagnosticReadback))
                .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
            ;
            auto readbackResult = m_graphics.createBuffer(desc);
            if(readbackResult)
                diagnostics.readback = Move(*readbackResult);
            else
                diagnostics.readback.reset();
            if(!readbackResult)
                return false;
        }
        const u64 contextSize = m_state.m_csg.bytes.size();
        if(contextSize == 0u || contextSize > maximumRange || contextSize > Limit<u32>::s_Max)
            return false;
        if(!snapshot.csgContext || snapshot.csgContext->getCreationDescription().byteSize < contextSize || !snapshot.csgOpaqueDepth || snapshot.csgOpaqueDepth->getCreationDescription().byteSize < plan.countByteSize){
            Array<Core::BufferHandle, 2u> csgBuffers{};
            Array<Core::GpuDescriptorHandle, 2u> csgDescriptors{};
            Core::GpuDescriptorRetirementGuard retireCsg(heap, Span<const Core::GpuDescriptorHandle>(csgDescriptors));
            const Array<u64, 2u> csgSizes = { contextSize, plan.countByteSize };
            const Array<Name, 2u> csgNames = { Name(s_LightSpaceResourceCsgContext), Name(s_LightSpaceResourceCsgOpaqueDepth) };
            for(u32 index = 0u; index < csgBuffers.size(); ++index){
                Core::BufferDesc desc;
                desc
                    .setByteSize(csgSizes[index]).setStructStride(sizeof(u32)).setCanHaveRawViews(true).setCanHaveUAVs(true)
                    .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(csgNames[index])
                    .enableAutomaticStateTracking(Core::ResourceStates::Common)
                ;
                auto csgBuffersResult = m_graphics.createBuffer(desc);
                if(csgBuffersResult)
                    csgBuffers[index] = Move(*csgBuffersResult);
                else
                    csgBuffers[index].reset();
                if(!csgBuffersResult)
                    return false;
                const auto descriptor = RayTracingDetail::RegisterHeapBuffer(heap, *csgBuffers[index], Core::GpuDescriptorClass::StorageBuffer, true);
                if(!descriptor)
                    return false;
                csgDescriptors[index] = *descriptor;
            }
            RayTracingDetail::RetireHeapHandle(heap, snapshot.csgContextDescriptor);
            RayTracingDetail::RetireHeapHandle(heap, snapshot.csgOpaqueDepthDescriptor);
            snapshot.csgContext = Move(csgBuffers[0]);
            diagnostics.contextInitialized = {};
            snapshot.csgOpaqueDepth = Move(csgBuffers[1]);
            snapshot.csgContextDescriptor = csgDescriptors[0];
            snapshot.csgOpaqueDepthDescriptor = csgDescriptors[1];
            for(auto& descriptor : csgDescriptors)
                descriptor = Core::GpuDescriptorHandle::Invalid();
            m_state.m_captureHistory.invalidate();
        }
    }
    if(
        snapshot.counts && snapshot.events && snapshot.views && snapshot.drawArguments && snapshot.depth
        && snapshot.counts->getCreationDescription().byteSize == plan.countByteSize
        && snapshot.events->getCreationDescription().byteSize == plan.eventByteSize
        && snapshot.views->getCreationDescription().byteSize == plan.viewByteSize
        && snapshot.drawArguments->getCreationDescription().byteSize == plan.drawArgumentByteSize
        && snapshot.depth->getCreationDescription().width == plan.textureResolution
        && snapshot.depth->getCreationDescription().arraySize == plan.viewCount
    )
        return true;
    Array<Core::BufferHandle, s_LightSpaceBufferCount> buffers{};
    Array<Core::GpuDescriptorHandle, s_LightSpaceDescriptorCount> descriptors{};
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, Span<const Core::GpuDescriptorHandle>(descriptors));
    const Array<Name, 4u> names = { Name(s_LightSpaceResourceCounts), Name(s_LightSpaceResourceEvents), Name(s_LightSpaceResourceViews), Name(s_LightSpaceResourceDrawArguments) };
    for(u32 index = 0u; index < buffers.size(); ++index){
        Core::BufferDesc desc;
        desc
            .setByteSize(sizes[index]).setStructStride(sizeof(u32)).setCanHaveRawViews(true).setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(index == s_LightSpaceDrawArgumentsBufferIndex)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(names[index])
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto buffersResult = m_graphics.createBuffer(desc);
        if(buffersResult)
            buffers[index] = Move(*buffersResult);
        else
            buffers[index].reset();
        if(!buffersResult)
            return false;
        const auto descriptor = RayTracingDetail::RegisterHeapBuffer(heap, *buffers[index], Core::GpuDescriptorClass::StorageBuffer, true);
        if(!descriptor)
            return false;
        descriptors[index] = *descriptor;
    }
    Core::TextureDesc depthDesc;
    depthDesc
        .setWidth(plan.textureResolution).setHeight(plan.textureResolution).setArraySize(plan.viewCount)
        .setDimension(Core::TextureDimension::Texture2DArray).setFormat(Core::Format::D32).setIsRenderTarget(true)
        .setInitialState(Core::ResourceStates::Common).setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName(Name(s_LightSpaceResourceDepth))
    ;
    auto depthResult = device.createTexture(depthDesc);
    if(!depthResult)
        return false;
    Core::TextureHandle depth = Move(*depthResult);
    if(!RendererDescriptorRegisterDetail::AssignSampledTexture(
        heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        depth.get(),
        Core::Format::D32,
        Core::TextureSubresourceSet(0u, 1u, 0u, plan.viewCount),
        Core::TextureDimension::Texture2DArray,
        descriptors[s_LightSpaceDepthDescriptorIndex]
    ))
        return false;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * s_PointLightViewCount> opaque;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * s_PointLightViewCount> transparent;
    for(u32 view = 0u; view < plan.viewCount; ++view){
        Core::FramebufferAttachment attachment;
        attachment.setTexture(depth.get()).setArraySlice(view);
        auto opaqueResult = device.createFramebuffer(Core::FramebufferDesc{}.setDepthAttachment(attachment));
        if(opaqueResult)
            opaque[view] = Move(*opaqueResult);
        else
            opaque[view].reset();
        attachment.setIsReadOnly(true);
        auto transparentResult = device.createFramebuffer(Core::FramebufferDesc{}.setDepthAttachment(attachment));
        if(transparentResult)
            transparent[view] = Move(*transparentResult);
        else
            transparent[view].reset();
        if(!opaqueResult || !transparentResult)
            return false;
    }
    RayTracingDetail::RetireHeapHandle(heap, snapshot.countsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.eventsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.viewsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.drawArgumentsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.depthDescriptor);
    m_state.m_captureHistory.invalidate();
    snapshot.counts = Move(buffers[s_LightSpaceCountsBufferIndex]);
    snapshot.events = Move(buffers[s_LightSpaceEventsBufferIndex]);
    snapshot.views = Move(buffers[s_LightSpaceViewsBufferIndex]);
    snapshot.drawArguments = Move(buffers[s_LightSpaceDrawArgumentsBufferIndex]);
    snapshot.depth = Move(depth);
    snapshot.countsDescriptor = descriptors[s_LightSpaceCountsBufferIndex];
    snapshot.eventsDescriptor = descriptors[s_LightSpaceEventsBufferIndex];
    snapshot.viewsDescriptor = descriptors[s_LightSpaceViewsBufferIndex];
    snapshot.drawArgumentsDescriptor = descriptors[s_LightSpaceDrawArgumentsBufferIndex];
    snapshot.depthDescriptor = descriptors[s_LightSpaceDepthDescriptorIndex];
    snapshot.opaqueFramebuffers = Move(opaque);
    snapshot.transparentFramebuffers = Move(transparent);
    for(auto& descriptor : descriptors)
        descriptor = Core::GpuDescriptorHandle::Invalid();
    return true;
}

void LightSpaceResourceSet::releaseLightSpaceShadowResources(){
    auto& state = m_state;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.countsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.eventsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.viewsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.drawArgumentsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.depthDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.csgContextDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.csgOpaqueDepthDescriptor);
    state.m_captureHistory.invalidate();
    state.m_diagnostics = {};
    state.m_captureSceneTrusted = false;
    state.m_captureReuseLogged = false;
    state.m_sceneBuffers.clear();
    state.m_captureBuffers.clear();
    state.m_sceneTextures.clear();
    state.m_captureTextures.clear();
    state.m_snapshot = {};
    state.m_casters.clear();
    for(auto& shader : state.m_shaders)
        shader = nullptr;
    state.m_sceneEligible = false;
    state.m_resourcesPrepared = false;
    state.m_pipelineFailed = false;
    state.m_dispatchLogged = false;
    state.m_csgDispatchLogged = false;
    state.m_csg.snapshot.hasCsg = false;
    state.m_csg.bytes.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LightSpaceShadowSceneGather::begin(const usize capacity, const b8 hardware){
    BeginLightSpaceCsgGather(m_state.m_csg, m_world, capacity, hardware);
    if(!hardware)
        m_state.m_sceneBuffers.reserve(capacity * (m_state.m_csg.gathering ? s_SceneBuffersPerCsgCandidate : s_SceneBuffersPerCandidate));
}

void LightSpaceShadowSceneGather::reserveSoftwareCasters(const usize capacity){
    m_softwareCasters.reserve(capacity);
}

void LightSpaceShadowSceneGather::appendSoftwareReceiver(
    const Core::ECS::EntityID entity, const b8 transparent, const SIMDMatrix& objectToWorld, const SIMDMatrix& worldToObject,
    const SIMDVectorSingle localMin, const SIMDVectorSingle localMax, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh
){
    RayTracingSoftwareBvhDetail::AppendLightSpaceSceneBuffers(mesh, m_state.m_csg.gathering, m_state.m_sceneBuffers);
    if(m_state.m_csg.gathering)
        AppendLightSpaceCsgReceiver(m_state.m_csg, entity, transparent, objectToWorld, worldToObject, localMin, localMax, mesh);
}

void LightSpaceShadowSceneGather::appendSoftwareCaster(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh){
    m_softwareCasters.push_back({
        .triangleIndexBuffer = mesh.triangleIndexBuffer,
        .meshletDescBuffer = mesh.meshletDescBuffer,
        .meshletBoundsBuffer = mesh.meshletLocalBoundsBuffer,
        .meshletCount = mesh.meshletCount,
        .meshletDescSlot = mesh.meshletCount != 0u ? mesh.meshletDescHeapHandle.slot() : 0u,
        .meshletBoundsSlot = mesh.meshletCount != 0u ? mesh.meshletLocalBoundsHeapHandle.slot() : 0u,
    });
}

void LightSpaceShadowSceneGather::appendHardwareReceiver(
    const Core::ECS::EntityID entity, const b8 transparent, const u32 instanceIndex, const SIMDMatrix& objectToWorld,
    const SIMDVectorSingle localMin, const SIMDVectorSingle localMax, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh
){
    if(!m_state.m_csg.gathering)
        return;
    SIMDVectorSingle determinant;
    const SIMDMatrix worldToObject = MatrixInverse(&determinant, objectToWorld);
    AppendLightSpaceCsgReceiver(m_state.m_csg, entity, transparent, objectToWorld, worldToObject, localMin, localMax, mesh);
    m_state.m_casters.push_back({
        .triangleIndexBuffer = mesh.triangleIndexBuffer,
        .meshletDescBuffer = mesh.meshletDescBuffer,
        .meshletBoundsBuffer = mesh.meshletLocalBoundsBuffer,
        .instanceIndex = instanceIndex,
        .indexCount = mesh.meshletPrimitiveIndexCount,
        .meshletCount = mesh.meshletCount,
        .meshletDescSlot = mesh.meshletCount != 0u ? mesh.meshletDescHeapHandle.slot() : 0u,
        .meshletBoundsSlot = mesh.meshletCount != 0u ? mesh.meshletLocalBoundsHeapHandle.slot() : 0u,
        .transparent = transparent,
        .csg = false,
    });
    RayTracingSoftwareBvhDetail::AppendLightSpaceSceneBuffers(mesh, true, m_state.m_sceneBuffers);
}

b8 LightSpaceShadowSceneGather::finishSoftware(const Span<const SceneSwBvhInstanceGpu> instances, const Span<NwbRtInstanceMaterialGpu> materials, const b8 contentComplete){
    if(!FinishLightSpaceCsgGather(m_registry, m_state.m_csg, m_world, m_scratchArena))
        return false;
    const auto& csg = m_state.m_csg.snapshot;
    const u32 instanceCount = static_cast<u32>(instances.size());
    m_state.m_sceneEligible = contentComplete && instanceCount <= NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK;
    m_state.m_casters.clear();
    m_state.m_casters.reserve(instanceCount);
    for(u32 index = 0u; index < instanceCount; ++index){
        const u64 indexCount = static_cast<u64>(instances[index].primitiveCount) * s_RayTracingTriangleIndexCount;
        LightSpaceShadowCaster& caster = m_softwareCasters[index];
        const Core::BufferHandle& indexBuffer = caster.triangleIndexBuffer;
        if(indexCount == 0u || indexCount > Limit<u32>::s_Max || !indexBuffer || !indexBuffer->getCreationDescription().isIndexBuffer || indexBuffer->getCreationDescription().byteSize < indexCount * sizeof(u32))
            m_state.m_sceneEligible = false;
        const b8 csgReceiver = csg.hasCsg && (csg.receiverRanges[index].flags & NWB_CSG_RAY_RECEIVER_ACTIVE) != 0u;
        if(csgReceiver)
            materials[index].flags |= NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW;
        caster.instanceIndex = index;
        caster.indexCount = static_cast<u32>(indexCount);
        caster.transparent = (materials[index].flags & RtInstanceMaterialFlag::Transparent) != 0u;
        caster.csg = csgReceiver;
        m_state.m_casters.push_back(Move(caster));
    }
    return true;
}

b8 LightSpaceShadowSceneGather::finishHardware(const Span<Core::RayTracingInstanceDesc> instances, const Span<NwbRtInstanceMaterialGpu> materials, const b8 contentComplete, b8& sceneHasTransparentOccluder){
    if(!FinishLightSpaceCsgGather(m_registry, m_state.m_csg, m_world, m_scratchArena))
        return false;
    const auto& csg = m_state.m_csg.snapshot;
    if(csg.hasCsg){
        if(csg.receiverRanges.size() != instances.size())
            return false;
        for(u32 index = 0u; index < static_cast<u32>(instances.size()); ++index){
            if((csg.receiverRanges[index].flags & NWB_CSG_RAY_RECEIVER_ACTIVE) == 0u)
                continue;
            const b8 transparent = (materials[index].flags & RtInstanceMaterialFlag::Transparent) != 0u;
            instances[index].setInstanceMask(ResolveRayTracingCsgInstanceMask(transparent));
            materials[index].flags |= NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW;
            m_state.m_casters[index].csg = true;
        }
        sceneHasTransparentOccluder = true;
    }
    m_state.m_sceneEligible = csg.hasCsg && contentComplete;
    if(!csg.hasCsg){
        m_state.m_casters.clear();
        m_state.m_sceneBuffers.clear();
    }
    return true;
}


void LightSpaceShadowSceneGather::appendSoftwareCaptureIdentity(
    const Core::ECS::EntityID entity, const b8 runtimeMesh, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh,
    const NwbRtInstanceMaterialGpu& material, const InstanceGpuData& instance, const RayTracingOpticalInstanceGpu& opticalInstance
){
    Fnv64AppendValue(m_captureSceneIdentity, entity.id);
    Fnv64AppendValue(m_captureSceneIdentity, mesh.positionBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.triangleIndexBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.attributeBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletCount);
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletDescBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletLocalBoundsBuffer.get());
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletDescHeapHandle.value);
    Fnv64AppendValue(m_captureSceneIdentity, mesh.meshletLocalBoundsHeapHandle.value);
    RayTracingSceneIdentityBuilder().updateSoftwareCapture(
        mesh.meshletPrimitiveIndexCount,
        mesh.runtimeMeshVersion,
        mesh.runtimeGeometryContentRevision,
        material,
        instance.translation.w,
        instance.geometryHeapSlots,
        opticalInstance,
        m_captureSceneIdentity
    );
    if(runtimeMesh && mesh.runtimeGeometryContentRevision == 0u)
        m_captureSceneTrusted = false;
}

void LightSpaceShadowSceneGather::publishSoftwareCaptureIdentity(
    const u32 instanceCount, const Span<const NwbRtInstanceMaterialGpu> materials, const Span<const InstanceGpuData> instances,
    const Span<const u8> materialBytes, const Span<const Core::TextureHandle> textures, const b8 contentComplete
){
    RayTracingSceneIdentityBuilder().appendMaterialBytes(instanceCount, materialBytes, m_captureSceneIdentity);
    for(const auto& texture : textures)
        Fnv64AppendValue(m_captureSceneIdentity, texture.get());
    m_state.m_sceneTextures.assign(textures.begin(), textures.end());
    const auto& csg = m_state.m_csg.snapshot;
    if(csg.hasCsg){
        Fnv64AppendValue(m_captureSceneIdentity, csg.identity);
        if(m_state.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseOneFrame || m_state.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseTwoFrames){
            m_captureSceneIdentity = BuildLightSpaceCsgCaptureIdentity(m_state.m_csg, materials, instances, materialBytes, textures);
            m_captureSceneTrusted = m_state.m_csg.captureGeometryTrusted;
        }
    }
    m_state.m_captureSceneIdentity = m_captureSceneIdentity;
    m_state.m_captureSceneTrusted = m_captureSceneTrusted && contentComplete;
}

void LightSpaceShadowSceneGather::publishHardwareCaptureIdentity(
    const u64 materialHash, const u64 tlasHash, const b8 staticScene, const b8 contentComplete,
    const Span<const NwbRtInstanceMaterialGpu> materials, const Span<const InstanceGpuData> instances,
    const Span<const u8> materialBytes, const Span<const Core::TextureHandle> textures
){
    const auto& csg = m_state.m_csg.snapshot;
    if(!csg.hasCsg)
        return;
    u64 captureIdentity = materialHash;
    Fnv64AppendValue(captureIdentity, tlasHash);
    Fnv64AppendValue(captureIdentity, csg.identity);
    for(const auto& buffer : m_state.m_sceneBuffers)
        Fnv64AppendValue(captureIdentity, buffer.get());
    for(const auto& caster : m_state.m_casters)
        RayTracingSceneIdentityBuilder().updateHardwareCaster(caster.meshletCount, caster.meshletDescSlot, caster.meshletBoundsSlot, captureIdentity);
    b8 captureTrusted = staticScene && contentComplete;
    if(m_state.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseOneFrame || m_state.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseTwoFrames){
        captureIdentity = BuildLightSpaceCsgCaptureIdentity(m_state.m_csg, materials, instances, materialBytes, textures);
        captureTrusted = m_state.m_csg.captureGeometryTrusted && contentComplete;
    }
    m_state.m_captureSceneIdentity = captureIdentity;
    m_state.m_captureSceneTrusted = captureTrusted;
    m_state.m_sceneTextures.assign(textures.begin(), textures.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_light_space_shadow{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Capture needs one simultaneous SRV/index state for each canonical buffer. Separate set uses would describe successive states.
[[nodiscard]] b8 GatherCaptureReads(const Core::GpuTaskGraph& graph, const LightSpaceShadowGraphInputs& inputs, Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& outUses){
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    if(!declarations.valid())
        return false;
    usize capacity = inputs.sceneReads.size() + inputs.snapshot.casters.size() + s_LightSpaceFixedResourceUseCount;
    for(const Core::GpuTaskResourceSetUse& use : inputs.sceneReadSets){
        if(!declarations.validResourceSet(use.resourceSet))
            return false;
        capacity += declarations.resourceSetAt(use.resourceSet.index).memberCount;
    }
    outUses.clear();
    outUses.reserve(capacity);
    const auto appendRead = [&](const Core::GpuTaskResourceUse& use){
        // The light-space scene inputs are whole-resource reads, including material texture sets.
        NWB_ASSERT(use.access == Core::GpuTaskResourceAccess::Read);
        NWB_ASSERT(use.range.bufferRange == Core::s_EntireBuffer && use.range.textureSubresources == Core::s_AllSubresources);
        for(auto& previous : outUses){
            if(previous.resource == use.resource){
                previous.requiredState |= use.requiredState;
                previous.hasIndependentStateSource = previous.hasIndependentStateSource && use.hasIndependentStateSource;
                return;
            }
        }
        outUses.push_back(use);
    };
    for(const Core::GpuTaskResourceUse& use : inputs.sceneReads)
        appendRead(use);
    for(const Core::GpuTaskResourceSetUse& use : inputs.sceneReadSets){
        const Core::GpuTaskGraphResourceSetView members = declarations.resourceSetAt(use.resourceSet.index);
        for(usize member = 0u; member < members.memberCount; ++member)
            appendRead(Core::GpuTaskResourceUse{ .resource = members.members[member], .range = use.range, .requiredState = use.requiredState, .access = use.access, .hasIndependentStateSource = use.hasIndependentStateSource, });
    }
    for(const auto& caster : inputs.snapshot.casters){
        // The scene geometry importer already retains this exact frozen canonical index buffer.
        const Core::GpuGraphResourceId resource = declarations.findImportedBuffer(caster.triangleIndexBuffer);
        if(!resource.valid())
            return false;
        appendRead(RendererTaskGraphDetail::ReadUse(resource, Core::ResourceStates::ShaderResource | Core::ResourceStates::IndexBuffer));
    }
    return true;
}

struct ViewTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        const b8& shadowPrepared;
        LightSpaceShadowSnapshot snapshot;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_LightSpaceShadowViews, payload.graphics.getDevice(), commandList);

        return RecordLightSpaceViews(payload.snapshot, commandList, payload.graphics.getDevice().getDescriptorHeap());
    }
};

struct ShadeTask{
    using Payload = ViewTask::Payload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_LightSpaceShadowShade, payload.graphics.getDevice(), commandList);

        if(!RecordLightSpaceShade(payload.snapshot, commandList, payload.graphics.getDevice().getDescriptorHeap()))
            return false;
        if(payload.snapshot.captureHistory)
            payload.snapshot.captureHistory->recordCapture(payload.snapshot.captureTicket);
        return true;
    }
};

struct DiagnosticTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, };


public:
    struct Payload{
        const b8& shadowPrepared;
        Core::BufferHandle context;
        Core::BufferHandle readback;
        LightSpaceShadowDiagnostics* diagnostics;
        u32 frameIndex;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        // The task's public state stays UAV; only this bounded copy temporarily reads the accumulated diagnostic word.
        commandList.setBufferState(payload.context.get(), Core::ResourceStates::CopySource);
        commandList.commitBarriers();
        commandList.copyBuffer(*payload.readback, 0u, *payload.context, NWB_CSG_SHADOW_RAY_FAILURE_OFFSET, sizeof(u32));
        commandList.setBufferState(payload.context.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        if(payload.shadowPrepared && payload.diagnostics && payload.diagnostics->readback == payload.readback){
            payload.diagnostics->submission = token;
            payload.diagnostics->acceptedFrame = payload.frameIndex;
        }
    }
};

struct CasterPayload{
    Core::GraphicsRuntime& graphics;
    const b8& shadowPrepared;
    LightSpaceShadowSnapshot snapshot;
    Vector<LightSpaceShadowCaster, Core::Alloc::GlobalArena> casters;


public:
    explicit CasterPayload(const LightSpaceShadowGraphInputs& inputs)
        : graphics(inputs.graphics)
        , shadowPrepared(inputs.shadowPrepared)
        , snapshot(inputs.snapshot)
        , casters(inputs.arena)
    {
        casters.assign(inputs.snapshot.casters.begin(), inputs.snapshot.casters.end());
        snapshot.casters = {};
    }


public:
    [[nodiscard]] LightSpaceShadowSnapshot frozenSnapshot()const noexcept{
        LightSpaceShadowSnapshot result = snapshot;
        result.casters = casters;
        return result;
    }
};

struct CullTask{
    using Payload = CasterPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_LightSpaceShadowCull, payload.graphics.getDevice(), commandList);

        return RecordLightSpaceCull(payload.frozenSnapshot(), commandList, payload.graphics.getDevice().getDescriptorHeap());
    }
};

struct CaptureTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics };


public:
    struct Payload : CasterPayload{
        b8 transparent;


    public:
        Payload(const LightSpaceShadowGraphInputs& inputs, const b8 transparent)
            : CasterPayload(inputs)
            , transparent(transparent)
        {}
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture)
            return false;
        if(!payload.shadowPrepared)
            return true;
        const LightSpaceShadowSnapshot snapshot = payload.frozenSnapshot();
        const auto& scope = payload.transparent ? RendererGpuTimingScope::s_LightSpaceShadowTransparentCapture : RendererGpuTimingScope::s_LightSpaceShadowOpaqueCapture;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), scope, payload.graphics.getDevice(), commandList);

        for(u32 view = 0u; view < snapshot.plan.viewCount; ++view){
            if(!RecordLightSpaceCapture(snapshot, view, payload.transparent, commandList, payload.graphics.getDevice().getDescriptorHeap()))
                return false;
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> DeclareLightSpaceShadowDiagnostics(const LightSpaceShadowGraphInputs& inputs, const Core::GpuGraphResourceId context, Core::GpuTaskGraph& graph){
    using namespace RendererTaskGraphDetail;
    const auto& snapshot = inputs.snapshot;
    if(!snapshot.ready || !snapshot.diagnostics || !snapshot.diagnosticReadback || !inputs.dependency.valid() || !context.valid())
        return MakeUnexpected(Failure{});
    const auto readbackResult = graph.importBuffer(
        snapshot.diagnosticReadback,
        BufferResourceDesc(s_LightSpaceDiagnosticsIdentity, s_LightSpaceDiagnosticsLabel)
            .setInitialState(Core::ResourceStates::CopyDest).setExternalFinalState(Core::ResourceStates::CopyDest)
    );
    if(!readbackResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId readback = *readbackResult;
    const Array<Core::GpuTaskResourceUse, 2u> uses{ ReadWriteUse(context, Core::ResourceStates::UnorderedAccess), WriteUse(readback, Core::ResourceStates::CopyDest), };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    EnableSameFamilyComputeEffectRouting(scheduling);
    EnableCrossFamilyComputeEffectRouting(scheduling);
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(s_LightSpaceDiagnosticsIdentity)
        .setMarkerLabel(s_LightSpaceDiagnosticsLabel)
        .setScheduling(scheduling).setDependencies({ &inputs.dependency, 1u })
        .setExternalStateSources(inputs.stateSources)
        .setResourceUses(uses)
    ;
    return graph.addTask<__hidden_task_graph_light_space_shadow::DiagnosticTask>(
        desc,
        __hidden_task_graph_light_space_shadow::DiagnosticTask::Payload{ inputs.shadowPrepared, snapshot.csgContext, snapshot.diagnosticReadback, snapshot.diagnostics, snapshot.diagnosticFrame, });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<LightSpaceShadowGraph> DeclareLightSpaceShadowMaps(const LightSpaceShadowGraphInputs& inputs, Core::GpuTaskGraph& graph){
    using namespace RendererTaskGraphDetail;
    const auto& snapshot = inputs.snapshot;
    if(!snapshot.ready || !inputs.dependency.valid() || snapshot.casters.empty())
        return MakeUnexpected(Failure{});
    LightSpaceShadowGraph result;
    const auto countsResult = ImportExternalBuffer(graph, snapshot.counts, Name(s_LightSpaceGraphCounts), "Light-Space Crossing Counts", Core::ResourceStates::Common, Core::ResourceStates::Common);
    const auto eventsResult = ImportExternalBuffer(graph, snapshot.events, Name(s_LightSpaceGraphEvents), "Light-Space Crossings", Core::ResourceStates::Common, Core::ResourceStates::Common);
    const auto viewsResult = ImportExternalBuffer(graph, snapshot.views, Name(s_LightSpaceGraphViews), "Light-Space Views", Core::ResourceStates::Common, Core::ResourceStates::Common);
    // Opaque capture clears every layer. Unknown preserves fresh Undefined; accepted state sources retain native states.
    const auto depthResult = graph.importTexture(snapshot.depth, TextureResourceDesc(Name(s_LightSpaceGraphDepth), "Light-Space Opaque Depth").setInitialState(Core::ResourceStates::Unknown).setExternalFinalState(Core::ResourceStates::Common));
    if(!countsResult || !eventsResult || !viewsResult || !depthResult)
        return MakeUnexpected(Failure{});
    result.counts = *countsResult;
    result.events = *eventsResult;
    result.views = *viewsResult;
    result.depth = *depthResult;
    const b8 csg = (snapshot.push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u;
    if(csg){
        const auto csgContextResult = ImportExternalBuffer(graph, snapshot.csgContext, Name(s_LightSpaceGraphCsgContext), "CSG Shadow Context", Core::ResourceStates::Common, Core::ResourceStates::Common);
        const auto csgOpaqueDepthResult = ImportExternalBuffer(graph, snapshot.csgOpaqueDepth, Name(s_LightSpaceGraphCsgDepth), "CSG Shadow Depth", Core::ResourceStates::Common, Core::ResourceStates::Common);
        if(!csgContextResult || !csgOpaqueDepthResult)
            return MakeUnexpected(Failure{});
        result.csgContext = *csgContextResult;
        result.csgOpaqueDepth = *csgOpaqueDepthResult;
    }
    if(snapshot.captureTicket.reuse){
        // Receiver tasks retain all current scene/BVH reads and the accepted native map state sources.
        result.ready = inputs.dependency;
        return result;
    }
    // A retained draw buffer is untouched during reuse; importing its final state would promise an unowned graph export.
    const auto drawArgumentsResult = ImportExternalBuffer(graph, snapshot.drawArguments, Name(s_LightSpaceGraphDrawArguments), "Light-Space Draw Arguments", Core::ResourceStates::Common, Core::ResourceStates::Common);
    if(!drawArgumentsResult)
        return MakeUnexpected(Failure{});
    result.drawArguments = *drawArgumentsResult;
    const auto uploadResult = graph.copyUploadData(snapshot.plan.views.data(), snapshot.plan.viewCount * sizeof(LightSpaceViewGpu), alignof(u32));
    if(!uploadResult)
        return MakeUnexpected(Failure{});
    const Core::GpuUploadBlobId upload = *uploadResult;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc uploadDesc;
    uploadDesc.setIdentity(s_LightSpaceViewUploadIdentity)
        .setMarkerLabel(s_LightSpaceViewUploadLabel)
        .setScheduling(scheduling)
        .setDependencies({ &inputs.dependency, 1u })
        .setExternalStateSources(inputs.stateSources);
    Core::GpuTaskId csgUpload;
    if(csg){
        if(!snapshot.diagnostics || snapshot.csgContextBytes.size() < NWB_CSG_RAY_CONTEXT_BYTES)
            return MakeUnexpected(Failure{});
        const b8 initialize = !snapshot.diagnostics->contextInitialized.valid();
        const usize prefixBytes = initialize ? snapshot.csgContextBytes.size() : NWB_CSG_SHADOW_RAY_FAILURE_OFFSET;
        const auto csgBytesResult = graph.copyUploadData(snapshot.csgContextBytes.data(), prefixBytes, alignof(u32));
        if(!csgBytesResult)
            return MakeUnexpected(Failure{});
        const Core::GpuUploadBlobId csgBytes = *csgBytesResult;
        uploadDesc.setIdentity(Name(s_LightSpaceGraphCsgUpload)).setMarkerLabel("CSG Shadow Context Upload");
        const auto csgUploadResult = graph.addUploadBufferTask(uploadDesc, Core::GpuUploadBufferTaskDesc{
            .source = csgBytes, .destination = result.csgContext, .finalState = Core::ResourceStates::Common,
            .acceptedToken = initialize ? &snapshot.diagnostics->contextInitialized : nullptr,
        });
        if(!csgUploadResult)
            return MakeUnexpected(Failure{});
        csgUpload = *csgUploadResult;
        if(!initialize){
            // Retain accumulated failures across new captures until the bounded asynchronous readback observes them.
            constexpr u32 s_PayloadOffset = NWB_CSG_SHADOW_RAY_FAILURE_OFFSET + sizeof(u32);
            const auto payloadResult = graph.copyUploadData(snapshot.csgContextBytes.data() + s_PayloadOffset, snapshot.csgContextBytes.size() - s_PayloadOffset, alignof(u32));
            if(!payloadResult)
                return MakeUnexpected(Failure{});
            const Core::GpuUploadBlobId payload = *payloadResult;
            uploadDesc.setIdentity(Name(s_LightSpaceGraphCsgPayloadUpload)).setMarkerLabel("CSG Shadow Payload Upload").setDependencies({ &csgUpload, 1u });
            const auto csgUploadResult2 = graph.addUploadBufferTask(uploadDesc, Core::GpuUploadBufferTaskDesc{ .source = payload, .destination = result.csgContext, .destinationOffsetBytes = s_PayloadOffset, .finalState = Core::ResourceStates::Common, });
            if(!csgUploadResult2)
                return MakeUnexpected(Failure{});
            csgUpload = *csgUploadResult2;
        }
        uploadDesc.setIdentity(s_LightSpaceViewUploadIdentity).setMarkerLabel(s_LightSpaceViewUploadLabel).setDependencies({ &csgUpload, 1u });
    }
    const auto viewUploadResult = graph.addUploadBufferTask(uploadDesc, Core::GpuUploadBufferTaskDesc{ .source = upload, .destination = result.views, .finalState = Core::ResourceStates::Common, });
    if(!viewUploadResult){
        result.viewUpload = {};
        return MakeUnexpected(Failure{});
    }
    result.viewUpload = *viewUploadResult;
    Core::GpuTaskDesc clearDesc;
    clearDesc.setIdentity(Name(s_LightSpaceGraphCountsClear))
        .setMarkerLabel("Light-Space Crossing Clear")
        .setScheduling(scheduling)
        .setDependencies({ &result.viewUpload, 1u })
        .setExternalStateSources(inputs.stateSources);
    const auto countsClearResult = graph.addClearBufferTask(clearDesc, Core::GpuClearBufferTaskDesc{ .destination = result.counts, .clearValue = 0u, });
    if(!countsClearResult){
        result.countsClear = {};
        return MakeUnexpected(Failure{});
    }
    result.countsClear = *countsClearResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses(inputs.scratchArena);
    uses.reserve(inputs.sceneReads.size() + s_LightSpaceFixedResourceUseCount);
    for(const Core::GpuTaskResourceUse& use : inputs.sceneReads)
        uses.push_back(use);
    if(csg)
        uses.push_back(ReadUse(result.csgContext, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadWriteUse(result.views, Core::ResourceStates::UnorderedAccess));
    Core::GpuTaskDesc viewDesc;
    viewDesc
        .setIdentity(Name(s_LightSpaceGraphViewFit))
        .setMarkerLabel("Light-Space View Fit")
        .setScheduling(scheduling)
        .setDependencies({ &result.countsClear, 1u })
        .setExternalStateSources(inputs.stateSources)
        .setResourceUses(uses)
        .setResourceSetUses(inputs.sceneReadSets)
    ;
    const auto viewFitResult = graph.addTask<__hidden_task_graph_light_space_shadow::ViewTask>(viewDesc, __hidden_task_graph_light_space_shadow::ViewTask::Payload{ inputs.graphics, inputs.shadowPrepared, snapshot, });
    if(!viewFitResult){
        result.viewFit = {};
        return MakeUnexpected(Failure{});
    }
    result.viewFit = *viewFitResult;

    uses.pop_back();
    uses.push_back(ReadUse(result.views, Core::ResourceStates::ShaderResource));
    uses.push_back(WriteUse(result.drawArguments, Core::ResourceStates::UnorderedAccess));
    for(const auto& caster : snapshot.casters){
        if(caster.meshletCount == 0u)
            continue;
        const Array<ReferenceWrapper<const Core::BufferHandle>, 2u> buffers = { caster.meshletDescBuffer, caster.meshletBoundsBuffer };
        for(const Core::BufferHandle& buffer : buffers){
            if(!buffer)
                return MakeUnexpected(Failure{});
            Core::GpuGraphResourceId resource;
            {
                const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
                resource = declarations.findImportedBuffer(buffer);
            }
            if(!resource.valid()){
                const auto importedResource = ImportExternalBuffer(graph, buffer, buffer->getCreationDescription().debugName, "Light-Space Meshlet Bounds", Core::ResourceStates::Common, Core::ResourceStates::Common);
                if(!importedResource)
                    return MakeUnexpected(Failure{});
                resource = *importedResource;
            }
            if(!resource.valid())
                return MakeUnexpected(Failure{});
            if(FindIf(uses.begin(), uses.end(), [&](const auto& use)noexcept{ return use.resource == resource; }) == uses.end())
                uses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }
    Core::GpuTaskDesc cullDesc;
    cullDesc
        .setIdentity(Name(s_LightSpaceGraphDrawCull))
        .setMarkerLabel("Light-Space Meshlet Cull")
        .setScheduling(scheduling)
        .setDependencies({ &result.viewFit, 1u })
        .setExternalStateSources(inputs.stateSources)
        .setResourceUses(uses)
        .setResourceSetUses(inputs.sceneReadSets)
    ;
    const auto drawCullResult = graph.addTask<__hidden_task_graph_light_space_shadow::CullTask>(cullDesc, __hidden_task_graph_light_space_shadow::CullTask::Payload(inputs));
    if(!drawCullResult){
        result.drawCull = {};
        return MakeUnexpected(Failure{});
    }
    result.drawCull = *drawCullResult;

    const Core::TextureSubresourceSet layers{ 0u, 1u, 0u, snapshot.plan.viewCount };
    if(!__hidden_task_graph_light_space_shadow::GatherCaptureReads(graph, inputs, uses))
        return MakeUnexpected(Failure{});
    if(csg)
        uses.push_back(ReadUse(result.csgContext, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadUse(result.views, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadUse(result.drawArguments, Core::ResourceStates::IndirectArgument));
    uses.push_back(WriteTextureUse(result.depth, layers, Core::ResourceStates::DepthWrite));
    scheduling.cost = Core::GpuTaskCostHint::Large;
    Core::GpuTaskDesc opaqueDesc;
    opaqueDesc
        .setIdentity(Name(s_LightSpaceGraphOpaqueCapture))
        .setMarkerLabel("Light-Space Opaque Capture")
        .setScheduling(scheduling)
        .setDependencies({ &result.drawCull, 1u })
        .setExternalStateSources(inputs.stateSources)
        .setResourceUses(uses)
    ;
    const auto opaqueCaptureResult = graph.addTask<__hidden_task_graph_light_space_shadow::CaptureTask>(opaqueDesc, __hidden_task_graph_light_space_shadow::CaptureTask::Payload(inputs, false));
    if(!opaqueCaptureResult){
        result.opaqueCapture = {};
        return MakeUnexpected(Failure{});
    }
    result.opaqueCapture = *opaqueCaptureResult;

    uses.pop_back();
    uses.push_back(ReadTextureUse(result.depth, layers, Core::ResourceStates::DepthRead));
    uses.push_back(ReadWriteUse(result.counts, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(result.events, Core::ResourceStates::UnorderedAccess));
    Core::GpuTaskDesc transparentDesc;
    transparentDesc
        .setIdentity(Name(s_LightSpaceGraphTransparentCapture))
        .setMarkerLabel("Light-Space Transparent Capture")
        .setScheduling(scheduling)
        .setDependencies({ &result.opaqueCapture, 1u })
        .setExternalStateSources(inputs.stateSources)
        .setResourceUses(uses)
    ;
    const auto transparentCaptureResult = graph.addTask<__hidden_task_graph_light_space_shadow::CaptureTask>(transparentDesc, __hidden_task_graph_light_space_shadow::CaptureTask::Payload(inputs, true));
    if(!transparentCaptureResult){
        result.transparentCapture = {};
        return MakeUnexpected(Failure{});
    }
    result.transparentCapture = *transparentCaptureResult;

    uses.clear();
    for(const Core::GpuTaskResourceUse& use : inputs.sceneReads)
        uses.push_back(use);
    if(csg)
        uses.push_back(ReadUse(result.csgContext, Core::ResourceStates::ShaderResource));
    uses.push_back(ReadUse(result.views, Core::ResourceStates::ShaderResource));
    uses.push_back(csg ? ReadWriteUse(result.counts, Core::ResourceStates::UnorderedAccess) : ReadUse(result.counts, Core::ResourceStates::ShaderResource));
    if(csg)
        uses.push_back(WriteUse(result.csgOpaqueDepth, Core::ResourceStates::UnorderedAccess));
    uses.push_back(ReadWriteUse(result.events, Core::ResourceStates::UnorderedAccess));
    Core::GpuTaskDesc shadeDesc;
    shadeDesc
        .setIdentity(Name(s_LightSpaceGraphShade))
        .setMarkerLabel("Light-Space Crossing Shade")
        .setScheduling(scheduling)
        .setDependencies({ &result.transparentCapture, 1u })
        .setExternalStateSources(inputs.stateSources)
        .setResourceUses(uses)
        .setResourceSetUses(inputs.sceneReadSets)
    ;
    const auto shadeResult = graph.addTask<__hidden_task_graph_light_space_shadow::ShadeTask>(shadeDesc, __hidden_task_graph_light_space_shadow::ShadeTask::Payload{ inputs.graphics, inputs.shadowPrepared, snapshot, });
    if(!shadeResult)
        return MakeUnexpected(shadeResult.error());
    result.shade = *shadeResult;
    result.ready = result.shade;
    if(!result.valid())
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPrepareGraphCoordinator::declareDeferredShadowPrepareTask(
    const ShadowPrepareGraphContext& context,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId materialContextSlots,
    const Span<const Core::GpuGraphResourceId> shadowTraceGeometryResources,
    const Span<const Core::GpuGraphResourceId> softwareBvhBuildStateResources,
    const b8 softwareTraceResourcesPrepared,
    Core::Alloc::ScratchArena& scratchArena,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace RendererTaskGraphDetail;

    m_deferredShadowPrepareSceneRefitTask = {};
    m_sceneBvhRefitInputsUploadTask = {};
    resetShadowPrepareTaskState();
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !context.rayTracing.shadowVisibilityResourcesPreflighted()
        || !currentBindlessSlots.valid()
        || !materialContextSlots.valid()
    )
        return false;

    ShadowPrepareUploadChain uploads;
    ShadowPrepareUploadChainBuilder uploadChainBuilder(context.graph, context.graphics, context.rayTracing);
    const b8 uploadsDeclared = uploadChainBuilder.declare(
        ShadowPrepareUploadInputs{
            .targets = deferredTargets,
            .rayTracingResources = rayTracingResources,
            .currentBindlessSlots = currentBindlessSlots,
            .materialContextSlots = materialContextSlots,
        },
        scratchArena,
        uploads
    );
    m_deferredBindlessSlotsUploadTask = uploads.tasks.bindlessSlots;
    m_rayTraceMaterialContextSlotsUploadTask = uploads.tasks.rayTraceMaterialContextSlots;
    m_csgTraceContextUploadTask = uploads.tasks.csgTraceContext;
    m_causticEmissionTargetsUploadTask = uploads.tasks.causticEmissionTargets;
    m_surfelFrameConstantsUploadTask = uploads.tasks.surfelFrameConstants;
    m_shadowInstanceMaterialUploadTask = uploads.tasks.shadowInstanceMaterial;
    m_shadowInstanceUploadTask = uploads.tasks.shadowInstance;
    m_shadowMaterialTypedUploadTask = uploads.tasks.shadowMaterialTyped;
    m_sceneBvhNodesUploadTask = uploads.tasks.sceneBvhNodes;
    m_sceneBvhInstancesUploadTask = uploads.tasks.sceneBvhInstances;
    if(!uploadsDeclared)
        return false;
    Core::GpuTaskId shadowPrepareDependency = uploads.completionTask;

    // Hardware TLAS builds retain their preflight instance stream; the first Graphics packet owns the native build.
    const b8 sceneTlasBuildGraphOwned = context.rayTracing.preparedSceneTlasBuildReady();
    if(sceneTlasBuildGraphOwned && !rayTracingShadowResources.sceneTlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen scene TLAS build has no imported acceleration structure"));
        return false;
    }
    const b8 meshBlasBuildsGraphOwned = context.rayTracing.preparedMeshBlasBuildsReady();
    const Span<const PreparedMeshBlasBuild> preparedMeshBlasBuilds = context.rayTracing.preparedMeshBlasBuilds();
    if(meshBlasBuildsGraphOwned && preparedMeshBlasBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no operations"));
        return false;
    }
    if(meshBlasBuildsGraphOwned && !rayTracingShadowResources.sceneTlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no scene TLAS"));
        return false;
    }
    const b8 meshSwBvhBuildsGraphOwned = context.rayTracing.preparedMeshSwBvhBuildsReady();
    const Span<const PreparedMeshSwBvhBuild> preparedMeshSwBvhBuilds = context.rayTracing.preparedMeshSwBvhBuilds();
    if(meshSwBvhBuildsGraphOwned && preparedMeshSwBvhBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build plan has no operations"));
        return false;
    }
    const b8 meshBlasGeometryBuildInputStatesGraphOwned = meshBlasBuildsGraphOwned && !softwareTraceResourcesPrepared && !meshSwBvhBuildsGraphOwned;
    // Software preparation uses typed sentinel clears before each mesh build.
    const b8 pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate = meshSwBvhBuildsGraphOwned && !context.rayTracing.shadowVisibilityHardwareSupported();

    PreparedMeshSwBvhGraphResourceVector pureSoftwareMeshSwBvhGraphResources{ scratchArena };
    ShadowPrepareResourcePlan shadowResourcePlan(scratchArena);
    const ShadowPrepareResourceInputs shadowResourceInputs{
        rayTracingShadowResources, uploads, currentBindlessSlots, materialContextSlots,
        shadowTraceGeometryResources, softwareBvhBuildStateResources, preparedMeshBlasBuilds,
        sceneTlasBuildGraphOwned, meshBlasBuildsGraphOwned, meshBlasGeometryBuildInputStatesGraphOwned
    };
    ShadowPrepareResourcePlanner shadowResourcePlanner(context.graph, context.rayTracing, context.mesh);
    if(!shadowResourcePlanner.prepare(shadowResourceInputs, scratchArena, shadowResourcePlan))
        return false;
    const auto& resourceUses = shadowResourcePlan.resourceUses;
    const auto& resourceSetUses = shadowResourcePlan.resourceSetUses;
    const auto& resourceSetUseCount = shadowResourcePlan.resourceSetUseCount;
    const auto& accelStructFinalizeResourceUses = shadowResourcePlan.accelStructFinalizeResourceUses;
    const auto& sceneTlas = shadowResourcePlan.sceneTlas;

    const b8 pureSoftwareMeshSwBvhBuildsGraphOwned = pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate;
    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
        auto resources = ResolvePreparedSoftwareBvhGraphResources(context.graph, preparedMeshSwBvhBuilds, scratchArena);
        if(!resources){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build is missing graph resources"));
            return false;
        }
        pureSoftwareMeshSwBvhGraphResources = Move(*resources);
    }

    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
        ShadowSoftwareBvhBuildChain buildChain(context.graph, context.rayTracing);
        if(!buildChain.declare(pureSoftwareMeshSwBvhGraphResources, timingTicket, shadowPrepareDependency, m_deferredShadowPrepareSoftwareBvhBuildFirstTask, m_deferredShadowPrepareSoftwareBvhBuildLastTask))
            return false;
    }

    if(const auto& sceneRefit = context.rayTracing.preparedSceneSwBvhRefit(); sceneRefit){
        // A scene refit requires every frozen mesh build to be declared before it.
        if(!context.rayTracing.preparedMeshSwBvhBuildPlanFrozen() || (!preparedMeshSwBvhBuilds.empty() && !pureSoftwareMeshSwBvhBuildsGraphOwned))
            return false;
        const auto refitTasks = DeclareSoftwareSceneRefit(sceneRefit, uploads.sceneBvhNodes, shadowPrepareDependency, context.graph, scratchArena);
        if(!refitTasks){
            m_deferredShadowPrepareSceneRefitTask = {};
            m_sceneBvhRefitInputsUploadTask = {};
            return false;
        }
        m_deferredShadowPrepareSceneRefitTask = refitTasks->refit;
        m_sceneBvhRefitInputsUploadTask = refitTasks->inputUpload;
        if(!m_deferredShadowPrepareSceneRefitTask.valid() || !m_sceneBvhRefitInputsUploadTask.valid())
            return false;
        shadowPrepareDependency = m_deferredShadowPrepareSceneRefitTask;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    // Software per-mesh chains precede this endpoint. Keep the whole packet even when later consumers form a frontier.
    scheduling.allowMergeAcrossConsumerFrontier = pureSoftwareMeshSwBvhBuildsGraphOwned || m_deferredShadowPrepareSceneRefitTask.valid();
    const Core::GpuTaskExternalStateSource shadowPrepareStateSource{ .states = context.persistentState.source(), };
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name(s_ShadowPreparePass))
        .setMarkerLabel("Shadow Preparation")
        .setScheduling(scheduling)
        .setDependencies({ &shadowPrepareDependency, s_ShadowPrepareSingleDependencyCount })
        .setExternalStateSources({ shadowPrepareStateSource.states ? &shadowPrepareStateSource : nullptr, shadowPrepareStateSource.states ? 1u : 0u })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ resourceSetUses.data(), resourceSetUseCount })
    ;
    const auto deferredShadowPrepareTaskResult = context.graph.addTask<ECSRenderDetail::ShadowPrepareGraphTask>(
        desc,
        ECSRenderDetail::ShadowPrepareGraphTask::Payload{
            .raytracingSystem = context.rayTracing,
            .outcome = m_shadowPreparationOutcome,
            .targets = deferredTargets,
            .timingTicket = timingTicket,
            .deferredBindlessSlotsWereUploaded = deferredTargets.bindless.slotsUploaded,
            .currentBindlessSlotsGraphOwned = uploads.currentBindlessSlotsGraphOwned,
            .shadowMaterialContextBatchGraphOwned = uploads.shadowMaterialContextBatchGraphOwned,
            .sceneBvhBatchGraphOwned = uploads.sceneBvhBatchGraphOwned,
            .sceneTlasBuildGraphOwned = sceneTlasBuildGraphOwned,
            .meshBlasBuildsGraphOwned = meshBlasBuildsGraphOwned,
            .meshBlasGeometryBuildInputStatesGraphOwned = meshBlasGeometryBuildInputStatesGraphOwned,
            .meshSwBvhBuildsGraphOwned = meshSwBvhBuildsGraphOwned,
            .preparedMeshSwBvhBuildsRecordedByGraph = pureSoftwareMeshSwBvhBuildsGraphOwned,
        });
    if(!deferredShadowPrepareTaskResult){
        m_deferredShadowPrepareTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation task"));
        return false;
    }
    m_deferredShadowPrepareTask = *deferredShadowPrepareTaskResult;
    const b8 accelStructBuildStatesGraphOwned = sceneTlasBuildGraphOwned || meshBlasBuildsGraphOwned;
    if(accelStructBuildStatesGraphOwned){
        ShadowAccelerationFinalizeBuilder finalizeBuilder(context.graph);
        if(!finalizeBuilder.declare(scratchArena, sceneTlasBuildGraphOwned, sceneTlas, preparedMeshBlasBuilds.size(), accelStructFinalizeResourceUses, m_deferredShadowPrepareTask, m_deferredShadowPrepareAccelStructFinalizeTask))
            return false;
    }
    return true;
}


void ShadowPrepareGraphCoordinator::resetShadowPrepareUploadTaskState()noexcept{
    m_deferredBindlessSlotsUploadTask = {};
    m_rayTraceMaterialContextSlotsUploadTask = {};
    m_csgTraceContextUploadTask = {};
    m_causticEmissionTargetsUploadTask = {};
    m_surfelFrameConstantsUploadTask = {};
    m_shadowInstanceMaterialUploadTask = {};
    m_shadowInstanceUploadTask = {};
    m_shadowMaterialTypedUploadTask = {};
    m_sceneBvhNodesUploadTask = {};
    m_sceneBvhInstancesUploadTask = {};
}

void ShadowPrepareGraphCoordinator::resetShadowPrepareTaskState()noexcept{
    m_deferredShadowPrepareTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildFirstTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildLastTask = {};
    m_deferredShadowPrepareAccelStructFinalizeTask = {};
    resetShadowPrepareUploadTaskState();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


struct ShadowPrepareAccelStructFinalizeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
    struct Payload{};


public:
    [[nodiscard]] static b8 Record([[maybe_unused]] const Payload& payload, [[maybe_unused]] Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context)noexcept{
        return true;
    }
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowAccelerationFinalizeBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const b8 sceneTlasBuildGraphOwned,
    const Core::GpuGraphResourceId sceneTlas,
    const usize preparedMeshBlasBuildCount,
    const Span<const Core::GpuTaskResourceUse> accelStructFinalizeResourceUses,
    const Core::GpuTaskId& prepareTask,
    Core::GpuTaskId& outTask
){
    const usize expectedFinalizeResourceUseCount = (sceneTlasBuildGraphOwned ? 1u : 0u) + preparedMeshBlasBuildCount;
    if((sceneTlasBuildGraphOwned && !sceneTlas.valid()) || accelStructFinalizeResourceUses.size() != expectedFinalizeResourceUseCount){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen acceleration-structure build has no final-state graph resources"));
        return false;
    }
    Core::GpuGraphResourceSetId accelStructFinalizeSet;
    if(!accelStructFinalizeResourceUses.empty()){
        Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> accelStructFinalizeResources{ scratchArena };
        accelStructFinalizeResources.reserve(accelStructFinalizeResourceUses.size());
        for(const Core::GpuTaskResourceUse& use : accelStructFinalizeResourceUses)
            accelStructFinalizeResources.push_back(use.resource);
        const auto accelStructFinalizeSetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name(s_ShadowPrepareAccelStructFinalizeResources))
                .setMarkerLabel("Shadow Prepare Accel-Struct Finalize Resources")
                .setMembers(accelStructFinalizeResources)
        );
        if(accelStructFinalizeSetResult)
            accelStructFinalizeSet = *accelStructFinalizeSetResult;
    }
    const b8 accelStructFinalizeSetGraphOwned = accelStructFinalizeSet.valid();
    const Core::GpuTaskResourceSetUse accelStructFinalizeSetUse{ .resourceSet = accelStructFinalizeSet, .range = {}, .requiredState = Core::ResourceStates::AccelStructRead, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskSchedulingHint accelStructFinalizeScheduling;
    accelStructFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    accelStructFinalizeScheduling.forceSubmissionBoundary = false;
    accelStructFinalizeScheduling.allowPacketMerge = true;
    accelStructFinalizeScheduling.mergeWithPrevious = true;
    // Compute consumers follow Shadow Preparation. Keep Read finalizers in the same packet so they wait on every build.
    accelStructFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc accelStructFinalizeDesc;
    accelStructFinalizeDesc
        .setIdentity(Name(s_ShadowPrepareAccelStructFinalize))
        .setMarkerLabel("Shadow Preparation Accel-Struct Finalize")
        .setScheduling(accelStructFinalizeScheduling)
        .setDependencies({ &prepareTask, 1u })
        // The typed final-state collection expands to the same inputs. Keep individuals when members form no unique set.
        .setResourceUses({ accelStructFinalizeSetGraphOwned ? nullptr : accelStructFinalizeResourceUses.data(), accelStructFinalizeSetGraphOwned ? 0u : accelStructFinalizeResourceUses.size() })
        .setResourceSetUses({ accelStructFinalizeSetGraphOwned ? &accelStructFinalizeSetUse : nullptr, accelStructFinalizeSetGraphOwned ? 1u : 0u })
    ;
    const auto deferredShadowPrepareAccelStructFinalizeTaskResult = m_graph.addTask< ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask >(
        accelStructFinalizeDesc,
        ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask::Payload{});
    if(!deferredShadowPrepareAccelStructFinalizeTaskResult){
        outTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare acceleration-structure final-state task"));
        return false;
    }
    outTask = *deferredShadowPrepareAccelStructFinalizeTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ShadowPrepareGeometryResources::prepareStorage(const b8 blasInputStatesGraphOwned){
    NWB_ASSERT(!m_storagePrepared);
    if(blasInputStatesGraphOwned){
        for(const PreparedMeshBlasBuild& build : m_inputs.blasBuilds){
            addRequest(build.positionBuffer);
            addRequest(build.triangleIndexBuffer);
        }
    }
    m_blasBuildInputs.reserve(m_requests ? m_requests->size() : m_inlineRequestCount);
    m_remainingTraceGeometry.reserve(m_inputs.traceResources.size());
    m_preparedBlasPolicy = blasInputStatesGraphOwned;
    m_storagePrepared = true;
}

b8 ShadowPrepareGeometryResources::gatherBuildInputs(const Core::GpuTaskGraph& graph){
    NWB_ASSERT(m_storagePrepared && !m_inputsGathered);
    if(!resolveRequests(graph)){
        clearBuildInputs();
        m_inputsGathered = true;
        return false;
    }
    if(m_preparedBlasPolicy){
        for(const PreparedMeshBlasBuild& build : m_inputs.blasBuilds){
            if(!appendBuildInput(build.positionBuffer) || !appendBuildInput(build.triangleIndexBuffer)){
                clearBuildInputs();
                m_inputsGathered = true;
                return false;
            }
        }
    }
    m_inputsGathered = true;
    return true;
}

b8 ShadowPrepareGeometryResources::gatherRemainingTraceResources()noexcept{
    NWB_ASSERT(m_inputsGathered);
    const b8 hasBlasInputs = !m_blasBuildInputs.empty();
    for(const Core::GpuGraphResourceId resource : m_inputs.traceResources){
        if(!resource.valid())
            return false;
        const BufferRequest* const request = hasBlasInputs ? findResolvedRequest(resource) : nullptr;
        if(request && request->selected)
            continue;
        m_remainingTraceGeometry.push_back(resource);
    }
    return true;
}

b8 ShadowPrepareGeometryResources::isPreparedMeshBlasBuild(const Name& meshName)const{
    if(!m_inputs.blasBuildsGraphOwned)
        return false;
    if(m_inputs.blasBuilds.size() <= s_SmallInlineIndexCapacity){
        const PreparedMeshBlasBuild* const builds = m_inputs.blasBuilds.data();
        for(usize index = 0u; index < m_inputs.blasBuilds.size(); ++index){
            if(builds[index].meshName == meshName)
                return true;
        }
        return false;
    }
    if(!m_namesPrepared)
        prepareNames();
    const NameHash& identity = meshName.identityHash();
    if(m_names)
        return m_names->find(MakeNotNull(&identity)) != m_names->end();
    for(usize index = 0u; index < m_inlineNameCount; ++index){
        if(*m_inlineNames[index] == identity)
            return true;
    }
    return false;
}

void ShadowPrepareGeometryResources::addRequest(const Core::BufferHandle& buffer){
    if(!buffer)
        return;
    BufferRequest* request = findRequest(buffer.get());
    if(!request){
        if(!m_requests && m_inlineRequestCount == s_SmallInlineIndexCapacity)
            promoteRequests();
        if(m_requests){
            const auto inserted = m_requests->try_emplace(MakeNotNull(buffer.get()));
            if(!inserted.second)
                TerminateInvariant();
            request = &inserted.first.value();
            request->source = &buffer;
        }
        else{
            InlineRequest& entry = m_inlineRequests[m_inlineRequestCount];
            entry.buffer = buffer.get();
            entry.request.source = &buffer;
            request = &entry.request;
            ++m_inlineRequestCount;
        }
    }
}

ShadowPrepareGeometryResources::BufferRequest* ShadowPrepareGeometryResources::findRequest(Core::Buffer* const buffer)noexcept{
    if(!buffer)
        return nullptr;
    if(m_requests){
        const auto found = m_requests->find(MakeNotNull(buffer));
        return found == m_requests->end() ? nullptr : &found.value();
    }
    for(usize index = 0u; index < m_inlineRequestCount; ++index){
        if(m_inlineRequests[index].buffer == buffer)
            return &m_inlineRequests[index].request;
    }
    return nullptr;
}

void ShadowPrepareGeometryResources::promoteRequests(){
    RequestIndex requests(s_PromotedInlineIndexCapacity, m_scratchArena);
    for(const InlineRequest& entry : m_inlineRequests){
        if(!requests.emplace(MakeNotNull(entry.buffer), entry.request).second)
            TerminateInvariant();
    }
    static_assert(IsNothrowMoveConstructible_V<RequestIndex>);
    m_requests.emplace(Move(requests));
}

b8 ShadowPrepareGeometryResources::resolveRequests(const Core::GpuTaskGraph& graph){
    const usize requestCount = m_requests ? m_requests->size() : m_inlineRequestCount;
    if(requestCount == 0u)
        return true;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return false;
        m_graphGeneration = declarations.generation();
        if(m_requests){
            for(auto request = m_requests->begin(); request != m_requests->end(); ++request)
                request.value().resource = declarations.findImportedBuffer(*request.value().source);
        }
        else{
            for(usize index = 0u; index < m_inlineRequestCount; ++index){
                BufferRequest& request = m_inlineRequests[index].request;
                request.resource = declarations.findImportedBuffer(*request.source);
            }
        }
    }
    if(m_requests){
        ResourceIndex resources(m_requests->bucket_count(), m_scratchArena);
        for(auto request = m_requests->begin(); request != m_requests->end(); ++request){
            if(request.value().resource.valid() && !resources.emplace(request.value().resource.index, MakeNotNull(&request.value())).second)
                TerminateInvariant();
        }
        static_assert(IsNothrowMoveConstructible_V<ResourceIndex>);
        m_resources.emplace(Move(resources));
    }
    // Only resolved requests gain trace membership; stale generations stay distinct.
    for(const Core::GpuGraphResourceId traceResource : m_inputs.traceResources){
        if(BufferRequest* const request = findResolvedRequest(traceResource))
            request->listedForTrace = true;
    }
    return true;
}

ShadowPrepareGeometryResources::BufferRequest* ShadowPrepareGeometryResources::findResolvedRequest(const Core::GpuGraphResourceId resource)noexcept{
    if(!resource.valid() || resource.generation != m_graphGeneration)
        return nullptr;
    if(m_resources){
        const auto found = m_resources->find(resource.index);
        return found == m_resources->end() ? nullptr : found.value().get();
    }
    for(usize index = 0u; index < m_inlineRequestCount; ++index){
        BufferRequest& request = m_inlineRequests[index].request;
        if(request.resource == resource)
            return &request;
    }
    return nullptr;
}

b8 ShadowPrepareGeometryResources::appendBuildInput(const Core::BufferHandle& buffer)noexcept{
    BufferRequest* const request = findRequest(buffer.get());
    if(!request || !request->resource.valid() || !request->listedForTrace)
        return false;
    if(!request->selected){
        m_blasBuildInputs.push_back(request->resource);
        request->selected = true;
    }
    return true;
}

void ShadowPrepareGeometryResources::clearBuildInputs()noexcept{
    m_blasBuildInputs.clear();
    if(m_requests){
        for(auto request = m_requests->begin(); request != m_requests->end(); ++request)
            request.value().selected = false;
    }
    else{
        for(usize index = 0u; index < m_inlineRequestCount; ++index)
            m_inlineRequests[index].request.selected = false;
    }
}

void ShadowPrepareGeometryResources::prepareNames()const{
    for(const PreparedMeshBlasBuild& build : m_inputs.blasBuilds){
        const NameHash& identity = build.meshName.identityHash();
        if(m_names){
            m_names->insert(MakeNotNull(&identity));
            continue;
        }
        b8 found = false;
        for(usize index = 0u; index < m_inlineNameCount; ++index){
            if(*m_inlineNames[index] == identity){
                found = true;
                break;
            }
        }
        if(found)
            continue;
        if(m_inlineNameCount < s_SmallInlineIndexCapacity){
            m_inlineNames[m_inlineNameCount] = &identity;
            ++m_inlineNameCount;
            continue;
        }
        NameIndex names(s_PromotedInlineIndexCapacity, m_scratchArena);
        for(const NameHash* const existing : m_inlineNames)
            names.insert(MakeNotNull(existing));
        names.insert(MakeNotNull(&identity));
        static_assert(IsNothrowMoveConstructible_V<NameIndex>);
        m_names.emplace(Move(names));
    }
    m_namesPrepared = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ShadowPreparePacketValidator::validate(const Core::GpuCompiledGraph::ReadView& compiledPlan, ShadowPreparePacketValidationResult& outResult)const noexcept{
    const RendererFramePipeline& pipeline = m_pipeline;
    // Software clears belong to the Shadow Preparation packet; a split would omit them.
    outResult.softwareBvhBuildsMerged =
        !pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid()
            ? !pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
            : (
                pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
                && compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareSoftwareBvhBuildFirstTask)
                && compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareSoftwareBvhBuildLastTask)
            )
    ;
    // The endpoint must never accept uploaded scene topology without its current-pose refit.
    outResult.softwareSceneRefitMerged =
        pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareSceneRefitTask.valid() == pipeline.m_shadowPrepareGraph.m_sceneBvhRefitInputsUploadTask.valid()
        && (!pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareSceneRefitTask.valid()
            || (compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareSceneRefitTask) && compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_sceneBvhRefitInputsUploadTask)))
    ;
    // Frozen transitions must share the build's submission for an atomic handoff.
    outResult.accelStructFinalizeMerged = !pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareAccelStructFinalizeTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareAccelStructFinalizeTask);
    // Selector uploads must stay in the Shadow Preparation packet.
    outResult.bindlessSlotsUploadMerged = !pipeline.m_shadowPrepareGraph.m_deferredBindlessSlotsUploadTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_deferredBindlessSlotsUploadTask);
    // Keep the upload in the first packet so it becomes the handoff.
    outResult.rayTraceMaterialContextSlotsUploadMerged =
        (!pipeline.m_shadowPrepareGraph.m_rayTraceMaterialContextSlotsUploadTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_rayTraceMaterialContextSlotsUploadTask))
        && (!pipeline.m_shadowPrepareGraph.m_csgTraceContextUploadTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_csgTraceContextUploadTask))
    ;
    // Nonempty caustic payloads must live in the Shadow Preparation packet.
    outResult.causticEmissionTargetsUploadMerged = !pipeline.m_shadowPrepareGraph.m_causticEmissionTargetsUploadTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_causticEmissionTargetsUploadTask);
    // Surfel constants must share the Shadow Preparation packet.
    outResult.surfelFrameConstantsUploadMerged = !pipeline.m_shadowPrepareGraph.m_surfelFrameConstantsUploadTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_surfelFrameConstantsUploadTask);
    // This triple must stay in the first accepted packet.
    outResult.shadowMaterialContextUploadsMerged =
        (!pipeline.m_shadowPrepareGraph.m_shadowInstanceMaterialUploadTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_shadowInstanceMaterialUploadTask))
        && (!pipeline.m_shadowPrepareGraph.m_shadowInstanceUploadTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_shadowInstanceUploadTask))
        && (!pipeline.m_shadowPrepareGraph.m_shadowMaterialTypedUploadTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_shadowMaterialTypedUploadTask))
    ;
    // Keep the pair in Shadow Preparation as the only producer.
    outResult.sceneBvhUploadsMerged =
        pipeline.m_shadowPrepareGraph.m_sceneBvhNodesUploadTask.valid() == pipeline.m_shadowPrepareGraph.m_sceneBvhInstancesUploadTask.valid()
        && (!pipeline.m_shadowPrepareGraph.m_sceneBvhNodesUploadTask.valid()
            || (compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_sceneBvhNodesUploadTask) && compiledPlan.tasksSharePacket(pipeline.m_shadowPrepareGraph.m_deferredShadowPrepareTask, pipeline.m_shadowPrepareGraph.m_sceneBvhInstancesUploadTask)))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPrepareResourcePlanner::prepare(const ShadowPrepareResourceInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareResourcePlan& result){
    using namespace RendererTaskGraphDetail;
    const auto& rayTracingShadowResources = inputs.rayTracingShadowResources;
    const auto& uploads = inputs.uploads;
    const auto currentBindlessSlots = inputs.currentBindlessSlots;
    const auto materialContextSlots = inputs.materialContextSlots;
    const auto& shadowTraceGeometryResources = inputs.shadowTraceGeometryResources;
    const auto& softwareBvhBuildStateResources = inputs.softwareBvhBuildStateResources;
    const auto& preparedMeshBlasBuilds = inputs.preparedMeshBlasBuilds;
    const auto sceneTlasBuildGraphOwned = inputs.sceneTlasBuildGraphOwned;
    const auto meshBlasBuildsGraphOwned = inputs.meshBlasBuildsGraphOwned;
    const auto meshBlasGeometryBuildInputStatesGraphOwned = inputs.meshBlasGeometryBuildInputStatesGraphOwned;
    auto& resourceUses = result.resourceUses;
    auto& resourceSetUses = result.resourceSetUses;
    auto& resourceSetUseCount = result.resourceSetUseCount;
    auto& accelStructFinalizeResourceUses = result.accelStructFinalizeResourceUses;
    auto& sceneTlas = result.sceneTlas;

    const ShadowPrepareGeometryInputs geometryInputs{ .blasBuilds = preparedMeshBlasBuilds, .traceResources = shadowTraceGeometryResources, .blasBuildsGraphOwned = meshBlasBuildsGraphOwned, };
    ShadowPrepareGeometryResources geometryResources(geometryInputs, scratchArena);
    const auto& meshBlasGeometryBuildInputResources = geometryResources.m_blasBuildInputs;
    const auto& shadowPrepareTraceGeometryResources = geometryResources.m_remainingTraceGeometry;
    ECSRenderDetail::MeshBlasGraphStateVector liveMeshBlasGraphStates{ scratchArena };
    m_meshSystem.collectBlasGraphStates(liveMeshBlasGraphStates);
    resourceUses.reserve(s_ShadowPrepareResourceUseCapacity + (uploads.csgTraceContext.valid() ? 1u : 0u) + shadowTraceGeometryResources.size() + softwareBvhBuildStateResources.size() + liveMeshBlasGraphStates.size() + preparedMeshBlasBuilds.size() * 2u);
    accelStructFinalizeResourceUses.reserve((sceneTlasBuildGraphOwned ? 1u : 0u) + preparedMeshBlasBuilds.size());
    geometryResources.prepareStorage(meshBlasGeometryBuildInputStatesGraphOwned);
    // Shadow Preparation owns post-transition boundaries; Compute readers wait on this packet.
    resourceUses.push_back(ReadWriteUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Retain Shadow Preparation as producer; WAW handoff retires the immutable upload.
    resourceUses.push_back(WriteUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(uploads.csgTraceContext.valid())
        resourceUses.push_back(WriteUse(uploads.csgTraceContext, Core::ResourceStates::ShaderResource));
    if(uploads.causticEmissionTargets.valid())
        resourceUses.push_back(WriteUse(uploads.causticEmissionTargets, Core::ResourceStates::ShaderResource));
    if(uploads.surfelFrameConstants.valid())
        resourceUses.push_back(WriteUse(uploads.surfelFrameConstants, Core::ResourceStates::ConstantBuffer));
    if(uploads.shadowInstanceMaterials.valid())
        resourceUses.push_back(WriteUse(uploads.shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
    if(uploads.shadowInstances.valid())
        resourceUses.push_back(WriteUse(uploads.shadowInstances, Core::ResourceStates::ShaderResource));
    if(uploads.shadowMaterialTyped.valid())
        resourceUses.push_back(WriteUse(uploads.shadowMaterialTyped, Core::ResourceStates::ShaderResource));
    if(uploads.sceneBvhNodes.valid())
        resourceUses.push_back(WriteUse(uploads.sceneBvhNodes, Core::ResourceStates::ShaderResource));
    if(uploads.sceneBvhInstances.valid())
        resourceUses.push_back(WriteUse(uploads.sceneBvhInstances, Core::ResourceStates::ShaderResource));

    b8 resourcesImported = true;
    if(!geometryResources.gatherBuildInputs(m_graph)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS geometry inputs are missing graph resources"));
        return false;
    }
    if(meshBlasBuildsGraphOwned){
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            const auto blasIdentity = DeriveName(build.meshName, s_BlasNameSuffix);
            if(!blasIdentity){
                resourcesImported = false;
                continue;
            }
            // First build knows native Common; retained storage imports the accepted binding.
            const Core::ResourceStates::Mask blasInitialState = build.backingFresh ? Core::ResourceStates::Common : Core::ResourceStates::Unknown;
            const auto blas = m_graph.importAccelStruct(build.blas, AccelStructResourceDesc(*blasIdentity, "Prepared Mesh BLAS").setInitialState(blasInitialState));
            resourcesImported = resourcesImported && blas.has_value();
            if(blas.has_value()){
                // Typed acceleration-structure uses lower through their backing allocation.
                resourceUses.push_back(ReadWriteUse(*blas, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(*blas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    Core::GpuGraphResourceSetId meshBlasGeometryBuildInputSet;
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputResources.empty()){
        const auto meshBlasGeometryBuildInputSetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name(s_ShadowPrepareBlasGeometryBuildInputs))
                .setMarkerLabel("Shadow Prepare BLAS Geometry Build Inputs")
                .setMembers(meshBlasGeometryBuildInputResources)
        );
        if(meshBlasGeometryBuildInputSetResult)
            meshBlasGeometryBuildInputSet = *meshBlasGeometryBuildInputSetResult;
    }
    const b8 meshBlasGeometryBuildInputSetGraphOwned = meshBlasGeometryBuildInputSet.valid();
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputSetGraphOwned){
        for(const Core::GpuGraphResourceId resource : meshBlasGeometryBuildInputResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::AccelStructBuildInput));
    }
    const Core::GpuTaskResourceSetUse meshBlasGeometryBuildInputSetUse{ .resourceSet = meshBlasGeometryBuildInputSet, .range = {}, .requiredState = Core::ResourceStates::AccelStructBuildInput, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    if(meshBlasGeometryBuildInputSetGraphOwned)
        resourceSetUses[resourceSetUseCount++] = meshBlasGeometryBuildInputSetUse;
    if(!geometryResources.gatherRemainingTraceResources())
        return false;


    // Frozen trace list may carry build inputs; keep them separate from the SRV subset.
    Core::GpuGraphResourceSetId shadowPrepareTraceGeometrySet;
    if(!shadowPrepareTraceGeometryResources.empty()){
        const auto shadowPrepareTraceGeometrySetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name(s_ShadowPrepareTraceGeometry))
                .setMarkerLabel("Shadow Prepare Trace Geometry")
                .setMembers(shadowPrepareTraceGeometryResources)
        );
        if(shadowPrepareTraceGeometrySetResult)
            shadowPrepareTraceGeometrySet = *shadowPrepareTraceGeometrySetResult;
    }
    const b8 shadowPrepareTraceGeometryStatesGraphOwned = shadowPrepareTraceGeometrySet.valid();
    if(!shadowPrepareTraceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : shadowPrepareTraceGeometryResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowPrepareTraceGeometrySetUse{ .resourceSet = shadowPrepareTraceGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    if(shadowPrepareTraceGeometryStatesGraphOwned)
        resourceSetUses[resourceSetUseCount++] = shadowPrepareTraceGeometrySetUse;
    for(const Core::GpuGraphResourceId resource : softwareBvhBuildStateResources){
        if(!resource.valid())
            return false;
    }
    Core::GpuGraphResourceSetId softwareBvhBuildStateSet;
    if(!softwareBvhBuildStateResources.empty()){
        const auto softwareBvhBuildStateSetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name(s_ShadowPrepareSoftwareBvhBuildState))
                .setMarkerLabel("Shadow Prepare Software BVH Build State")
                .setMembers(softwareBvhBuildStateResources)
        );
        if(softwareBvhBuildStateSetResult)
            softwareBvhBuildStateSet = *softwareBvhBuildStateSetResult;
    }
    const b8 softwareBvhBuildStateStatesGraphOwned = softwareBvhBuildStateSet.valid();
    if(!softwareBvhBuildStateStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : softwareBvhBuildStateResources){
            // Parent links and scratch keep native UAV state; they are state-only resources.
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        }
    }
    const Core::GpuTaskResourceSetUse softwareBvhBuildStateSetUse{ .resourceSet = softwareBvhBuildStateSet, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    if(softwareBvhBuildStateStatesGraphOwned)
        resourceSetUses[resourceSetUseCount++] = softwareBvhBuildStateSetUse;

    if(rayTracingShadowResources.sceneTlas){
        // Imports observe the backing generation; retained storage stays Unknown until accepted.
        const Core::ResourceStates::Mask sceneTlasInitialState = m_raytracingSystem.sceneTlasBackingInitialState();
        const auto sceneTlasResult = m_graph.importAccelStruct(
            rayTracingShadowResources.sceneTlas,
            AccelStructResourceDesc(Name(s_SceneTlasResourceName), s_SceneTlasResourceLabel).setInitialState(sceneTlasInitialState)
        );
        if(sceneTlasResult)
            sceneTlas = *sceneTlasResult;
        resourcesImported = resourcesImported && sceneTlasResult.has_value();
        if(sceneTlas.valid()){
            if(sceneTlasBuildGraphOwned){
                // Frozen recorder only builds; graph lowers Write entry, successor lowers Read handoff.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // Cached scene TLAS remains read-ready; retain its accepted-state handoff.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    for(const ECSRenderDetail::MeshBlasGraphState& state : liveMeshBlasGraphStates){
        // Frozen plans own retained handles; never collide replacements with frozen identities.
        if(geometryResources.isPreparedMeshBlasBuild(state.meshName))
            continue;

        const auto blasIdentity = DeriveName(state.meshName, s_BlasNameSuffix);
        if(!blasIdentity){
            resourcesImported = false;
            continue;
        }
        const Core::ResourceStates::Mask blasInitialState = state.backingFresh ? Core::ResourceStates::Common : Core::ResourceStates::Unknown;
        const auto blas = m_graph.importAccelStruct(state.blas, AccelStructResourceDesc(*blasIdentity, "Mesh BLAS").setInitialState(blasInitialState));
        resourcesImported = resourcesImported && blas.has_value();
        if(blas.has_value()){
            if(state.nativeBuildsBlas){
                // Retained backing keeps its accepted read state until the selected build is published.
                resourceUses.push_back(ReadWriteUse(*blas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // State-only import; rejected preparation re-pends and seeds accepted state.
                resourceUses.push_back(ReadUse(*blas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    if(!resourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow-preparation resources"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_prepare_upload_chain{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<Core::GpuGraphResourceId> ImportOptionalExternalBuffer(
    Core::GpuTaskGraph& graph,
    const Core::BufferHandle& buffer,
    const Name identity,
    const AStringView label
){
    if(!buffer)
        return Core::GpuGraphResourceId{};
    return RendererTaskGraphDetail::ImportExternalBuffer(graph, buffer, identity, label);
}

[[nodiscard]] inline b8 AppendChainedUploadBufferTask(
    Core::GpuTaskGraph& graph,
    const Name identity,
    const AStringView markerLabel,
    const Core::GpuTaskCostHint::Enum cost,
    const Core::GpuUploadBlobId source,
    const Core::GpuGraphResourceId destination,
    const tchar* const warningMessage,
    Core::GpuTaskId& inOutDependencyTask,
    Core::GpuTaskId& outTask
){
    Core::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = cost;
    uploadScheduling.forceSubmissionBoundary = false;
    uploadScheduling.allowPacketMerge = true;
    uploadScheduling.mergeWithPrevious = true;

    Core::GpuTaskDesc uploadDesc;
    uploadDesc.setIdentity(identity).setMarkerLabel(markerLabel).setScheduling(uploadScheduling).setDependencies({ &inOutDependencyTask, 1u });
    const auto uploadTaskResult = graph.addUploadBufferTask(
        uploadDesc,
        Core::GpuUploadBufferTaskDesc{ .source = source, .destination = destination, .finalState = Core::ResourceStates::Common, });
    if(!uploadTaskResult){
        outTask = {};
        NWB_LOGGER_WARNING(warningMessage);
        return false;
    }
    outTask = *uploadTaskResult;
    inOutDependencyTask = outTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPrepareUploadChainBuilder::declareSelectorAndContextUploads(const ShadowPrepareUploadInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareUploadChain& chain){
    using namespace RendererTaskGraphDetail;

    chain.currentBindlessSlotsGraphOwned = !inputs.targets.bindless.slotsUploaded;
    if(chain.currentBindlessSlotsGraphOwned){
        const auto bindlessSlotsBlobResult = m_graph.copyUploadData(&inputs.targets.bindless.slots, sizeof(inputs.targets.bindless.slots), alignof(DeferredBindlessResourceSlots));
        if(!bindlessSlotsBlobResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain deferred bindless selector upload data"));
            return false;
        }
        const Core::GpuUploadBlobId bindlessSlotsBlob = *bindlessSlotsBlobResult;

        Core::GpuTaskSchedulingHint uploadScheduling;
        uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        uploadScheduling.forceSubmissionBoundary = false;
        uploadScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc uploadDesc;
        uploadDesc.setIdentity(Name(s_ShadowDeferredBindlessSlotsUpload)).setMarkerLabel("Deferred Bindless Slots Upload").setScheduling(uploadScheduling);
        const auto deferredBindlessSlotsUploadTaskResult = m_graph.addUploadBufferTask(
            uploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = bindlessSlotsBlob,
                .destination = inputs.currentBindlessSlots,
                // Selector persists across packet closes; upload publishes ConstantBuffer.
                .finalState = Core::ResourceStates::ConstantBuffer,
            });
        if(!deferredBindlessSlotsUploadTaskResult){
            chain.tasks.bindlessSlots = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred bindless selector upload"));
            return false;
        }
        chain.tasks.bindlessSlots = *deferredBindlessSlotsUploadTaskResult;
    }

    // Trace buffers finalized by preflight; retain slots before compile.
    const auto rayTraceMaterialContextSlots = m_raytracingSystem.snapshotRayTraceMaterialContextSlots();
    if(!rayTraceMaterialContextSlots){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot ray-trace material-context selector"));
        return false;
    }
    const auto rayTraceMaterialContextSlotsBlobResult = m_graph.copyUploadData(&*rayTraceMaterialContextSlots, sizeof(*rayTraceMaterialContextSlots), alignof(RayTraceMaterialContextSlots));
    if(!rayTraceMaterialContextSlotsBlobResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain ray-trace material-context selector upload data"));
        return false;
    }
    const Core::GpuUploadBlobId rayTraceMaterialContextSlotsBlob = *rayTraceMaterialContextSlotsBlobResult;

    Vector<Core::GpuExternalCompletionId, Core::Alloc::ScratchArena> csgReadCompletions{ scratchArena };
    if(!inputs.rayTracingResources.rayTraceContextReadSubmissionTokens.empty()){
        csgReadCompletions.reserve(inputs.rayTracingResources.rayTraceContextReadSubmissionTokens.size());
        for(const auto& token : inputs.rayTracingResources.rayTraceContextReadSubmissionTokens){
            if(!token.valid())
                continue;
            const AString<Core::Alloc::ScratchArena> completionName = StringFormat(scratchArena, "render.raytrace.csg_queue_{}.read_complete", token.physicalQueueIndex);
            Core::GpuExternalCompletionDesc readCompleteDesc;
            const auto completionIdentity = ToName(completionName);
            if(!completionIdentity || !*completionIdentity)
                return false;
            readCompleteDesc.setIdentity(**completionIdentity).setMarkerLabel("Prior CSG Ray Read Complete").setToken(token);
            const auto readCompleteResult = m_graph.importExternalCompletion(readCompleteDesc);
            if(!readCompleteResult)
                return false;
            const Core::GpuExternalCompletionId readComplete = *readCompleteResult;
            csgReadCompletions.push_back(readComplete);
        }
    }

    const Core::GpuTaskId* const materialContextUploadDependencies = chain.currentBindlessSlotsGraphOwned ? &chain.tasks.bindlessSlots : nullptr;
    const usize materialContextUploadDependencyCount = chain.currentBindlessSlotsGraphOwned ? 1u : 0u;
    Core::GpuTaskSchedulingHint materialContextUploadScheduling;
    materialContextUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    materialContextUploadScheduling.forceSubmissionBoundary = false;
    materialContextUploadScheduling.allowPacketMerge = true;
    materialContextUploadScheduling.mergeWithPrevious = chain.currentBindlessSlotsGraphOwned;
    Core::GpuTaskDesc materialContextUploadDesc;
    materialContextUploadDesc
        .setIdentity(Name(s_ShadowRaytraceMaterialContextSlotsUpload))
        .setMarkerLabel("Ray-Trace Material Context Slots Upload")
        .setScheduling(materialContextUploadScheduling)
        .setDependencies({ materialContextUploadDependencies, materialContextUploadDependencyCount })
        // Old CSG rays read this persistent selector as well as their retained context buffer.
        .setExternalDependencies(csgReadCompletions)
    ;
    const auto rayTraceMaterialContextSlotsUploadTaskResult = m_graph.addUploadBufferTask(
        materialContextUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = rayTraceMaterialContextSlotsBlob,
            .destination = inputs.materialContextSlots,
            // Selector buffers publish Common; Shadow Preparation owns the next transition.
            .finalState = Core::ResourceStates::Common,
        });
    if(!rayTraceMaterialContextSlotsUploadTaskResult){
        chain.tasks.rayTraceMaterialContextSlots = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare ray-trace material-context selector upload"));
        return false;
    }
    chain.tasks.rayTraceMaterialContextSlots = *rayTraceMaterialContextSlotsUploadTaskResult;
    const Name causticEmissionTargetsIdentity = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
            ? Name(s_HardwareCausticEmissionTargetsName)
            : Name(s_SoftwareCausticEmissionTargetsName)
    ;
    const auto causticEmissionTargetsResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.causticEmissionTargetBuffer, causticEmissionTargetsIdentity, s_CausticEmissionTargetsLabel);
    if(!causticEmissionTargetsResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted caustic emission targets"));
        return false;
    }
    chain.causticEmissionTargets = *causticEmissionTargetsResult;

    const auto causticEmissionTargetsBlob = m_raytracingSystem.retainPreparedCausticEmissionTargetUpload(m_graph);
    if(!causticEmissionTargetsBlob){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted caustic emission-target upload data"));
        return false;
    }

    chain.completionTask = chain.tasks.rayTraceMaterialContextSlots;
    Expected<Core::GpuGraphResourceId> csgTraceContextResult = MakeUnexpected(Failure{});
    if(inputs.rayTracingResources.csgTraceContextBuffer){
        Core::GpuGraphResourceDesc contextDesc = BufferResourceDesc(Name(s_ShadowRaytraceCsgContext), "Current CSG Ray Context");
        csgTraceContextResult = m_graph.importBuffer(inputs.rayTracingResources.csgTraceContextBuffer, contextDesc);
        if(csgTraceContextResult)
            chain.csgTraceContext = *csgTraceContextResult;
    }
    const auto csgTraceContextBlob = m_raytracingSystem.retainPreparedCsgTraceContextUpload(m_graph);
    if(!csgTraceContextBlob || (inputs.rayTracingResources.csgTraceContextBuffer && !csgTraceContextResult)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: could not retain current CSG ray context"));
        return false;
    }
    if(*csgTraceContextBlob){
        if(!chain.csgTraceContext.valid())
            return false;
        Core::GpuTaskSchedulingHint csgUploadScheduling = materialContextUploadScheduling;
        csgUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        csgUploadScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc csgUploadDesc;
        csgUploadDesc
            .setIdentity(Name(s_ShadowRaytraceCsgContextUpload))
            .setMarkerLabel("Current CSG Ray Context Upload")
            .setScheduling(csgUploadScheduling)
            .setDependencies({ &chain.completionTask, 1u })
            .setExternalDependencies(csgReadCompletions)
        ;
        const auto csgTraceContextUploadTaskResult = m_graph.addUploadBufferTask(csgUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = **csgTraceContextBlob, .destination = chain.csgTraceContext, .finalState = Core::ResourceStates::Common, });
        if(!csgTraceContextUploadTaskResult){
            chain.tasks.csgTraceContext = {};
            return false;
        }
        chain.tasks.csgTraceContext = *csgTraceContextUploadTaskResult;
        chain.completionTask = chain.tasks.csgTraceContext;
    }
    if(*causticEmissionTargetsBlob){
        if(!chain.causticEmissionTargets.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic emission-target upload has no imported destination"));
            return false;
        }

        if(!__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
            m_graph,
            Name(s_ShadowRaytraceCausticEmissionTargetsUpload),
            "Caustic Emission Targets Upload",
            Core::GpuTaskCostHint::Medium,
            **causticEmissionTargetsBlob,
            chain.causticEmissionTargets,
            NWB_TEXT("RendererSystem: could not declare caustic emission-target upload"),
            chain.completionTask,
            chain.tasks.causticEmissionTargets
        ))
            return false;
    }
    return true;
}

b8 ShadowPrepareUploadChainBuilder::declareSurfelFrameConstantsUpload(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    const auto surfelFrameConstantsResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.surfelFrameConstantsBuffer, Name(s_SurfelConstantsName), s_SurfelConstantsLabel);
    if(!surfelFrameConstantsResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted surfel constants"));
        return false;
    }
    chain.surfelFrameConstants = *surfelFrameConstantsResult;

    const auto surfelFrameConstantsBlob = m_raytracingSystem.retainPreparedSurfelFrameConstantsUpload(m_graph, inputs.targets);
    if(!surfelFrameConstantsBlob){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted surfel-frame constants upload data"));
        return false;
    }
    if(*surfelFrameConstantsBlob){
        if(!chain.surfelFrameConstants.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel-frame constants upload has no imported destination"));
            return false;
        }
        if(!__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
            m_graph,
            Name(s_ShadowSurfelConstantsUpload),
            "Surfel Frame Constants Upload",
            Core::GpuTaskCostHint::Tiny,
            **surfelFrameConstantsBlob,
            chain.surfelFrameConstants,
            NWB_TEXT("RendererSystem: could not declare surfel-frame constants upload"),
            chain.completionTask,
            chain.tasks.surfelFrameConstants
        ))
            return false;
    }
    return true;
}

b8 ShadowPrepareUploadChainBuilder::declareShadowMaterialContextUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    const auto shadowInstanceMaterialsResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.shadowInstanceMaterialBuffer, Name(s_SceneShadowInstanceMaterialName), s_SceneShadowInstanceMaterialLabel);
    const auto shadowInstancesResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.shadowInstanceBuffer, Name(s_SceneShadowInstancesName), s_SceneShadowInstancesLabel);
    const auto shadowMaterialTypedResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.shadowMaterialTypedBuffer, Name(s_SceneShadowMaterialTypedName), s_SceneShadowMaterialTypedLabel);
    if(!shadowInstanceMaterialsResult || !shadowInstancesResult || !shadowMaterialTypedResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow material-context buffers"));
        return false;
    }
    chain.shadowMaterialTyped = *shadowMaterialTypedResult;
    chain.shadowInstances = *shadowInstancesResult;
    chain.shadowInstanceMaterials = *shadowInstanceMaterialsResult;

    const auto shadowMaterialContextUploads = m_raytracingSystem.retainPreparedShadowMaterialContextUploads(m_graph);
    if(!shadowMaterialContextUploads){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted shadow material-context upload data"));
        return false;
    }
    const Core::GpuUploadBlobId shadowInstanceMaterialsBlob = shadowMaterialContextUploads->instanceMaterial;
    const Core::GpuUploadBlobId shadowInstancesBlob = shadowMaterialContextUploads->instance;
    const Core::GpuUploadBlobId shadowMaterialTypedBlob = shadowMaterialContextUploads->materialTyped;
    chain.shadowMaterialContextBatchGraphOwned = shadowInstanceMaterialsBlob.valid();
    if(chain.shadowMaterialContextBatchGraphOwned != shadowInstancesBlob.valid() || chain.shadowMaterialContextBatchGraphOwned != shadowMaterialTypedBlob.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: incomplete frozen shadow material-context upload batch"));
        return false;
    }
    if(chain.shadowMaterialContextBatchGraphOwned){
        if(!chain.shadowInstanceMaterials.valid() || !chain.shadowInstances.valid() || !chain.shadowMaterialTyped.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen shadow material-context batch has no imported destination"));
            return false;
        }
        if(
            !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name(s_ShadowDeferredEffectsInstanceMaterialUpload),
                "Shadow Instance Materials Upload",
                Core::GpuTaskCostHint::Medium,
                shadowInstanceMaterialsBlob,
                chain.shadowInstanceMaterials,
                NWB_TEXT("RendererSystem: could not declare shadow instance-material upload"),
                chain.completionTask,
                chain.tasks.shadowInstanceMaterial
            )
            || !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name(s_ShadowDeferredEffectsShadowInstancesUpload),
                "Shadow Instances Upload",
                Core::GpuTaskCostHint::Medium,
                shadowInstancesBlob,
                chain.shadowInstances,
                NWB_TEXT("RendererSystem: could not declare shadow instance upload"),
                chain.completionTask,
                chain.tasks.shadowInstance
            )
            || !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name(s_ShadowDeferredEffectsMaterialTypedUpload),
                "Shadow Typed Materials Upload",
                Core::GpuTaskCostHint::Medium,
                shadowMaterialTypedBlob,
                chain.shadowMaterialTyped,
                NWB_TEXT("RendererSystem: could not declare shadow typed-material upload"),
                chain.completionTask,
                chain.tasks.shadowMaterialTyped
            )
        )
            return false;
    }
    return true;
}

b8 ShadowPrepareUploadChainBuilder::declareSceneBvhUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    const auto sceneBvhNodesResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.sceneBvhNodeBuffer, Name(s_SceneBvhNodesName), s_SceneBvhNodesLabel);
    const auto sceneBvhInstancesResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.sceneInstanceBuffer, Name(s_SceneInstancesName), s_SceneInstancesLabel);
    if(!sceneBvhNodesResult || !sceneBvhInstancesResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted software scene-BVH buffers"));
        return false;
    }
    chain.sceneBvhInstances = *sceneBvhInstancesResult;
    chain.sceneBvhNodes = *sceneBvhNodesResult;

    const auto sceneBvhUploads = m_raytracingSystem.retainPreparedSceneBvhUploads(m_graph);
    if(!sceneBvhUploads){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted software scene-BVH upload data"));
        return false;
    }
    const Core::GpuUploadBlobId sceneBvhNodesBlob = sceneBvhUploads->node;
    const Core::GpuUploadBlobId sceneBvhInstancesBlob = sceneBvhUploads->instance;
    chain.sceneBvhBatchGraphOwned = sceneBvhNodesBlob.valid();
    if(chain.sceneBvhBatchGraphOwned != sceneBvhInstancesBlob.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: incomplete frozen software scene-BVH upload pair"));
        return false;
    }
    if(chain.sceneBvhBatchGraphOwned){
        if(!chain.sceneBvhNodes.valid() || !chain.sceneBvhInstances.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene-BVH pair has no imported destination"));
            return false;
        }
        if(
            !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name(s_ShadowPassSceneBvhNodesUpload),
                "Scene BVH Nodes Upload",
                Core::GpuTaskCostHint::Medium,
                sceneBvhNodesBlob,
                chain.sceneBvhNodes,
                NWB_TEXT("RendererSystem: could not declare software scene-BVH node upload"),
                chain.completionTask,
                chain.tasks.sceneBvhNodes
            )
            || !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name(s_ShadowPassSceneInstancesUpload),
                "Scene BVH Instances Upload",
                Core::GpuTaskCostHint::Medium,
                sceneBvhInstancesBlob,
                chain.sceneBvhInstances,
                NWB_TEXT("RendererSystem: could not declare software scene-BVH instance upload"),
                chain.completionTask,
                chain.tasks.sceneBvhInstances
            )
        )
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowSoftwareBvhBuildChain::declare(
    const Span<const PreparedMeshSwBvhGraphResources> pureSoftwareMeshSwBvhGraphResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Core::GpuTaskId& shadowPrepareDependency,
    Core::GpuTaskId& firstTask,
    Core::GpuTaskId& lastTask
){
    using namespace RendererTaskGraphDetail;

    // Operations share scratch; keep clears adjacent in the accepting packet.
    Core::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    clearScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskSchedulingHint buildScheduling = clearScheduling;
    buildScheduling.cost = Core::GpuTaskCostHint::Large;

    Core::GpuTaskId buildDependency = shadowPrepareDependency;
    const auto addPureSoftwareBvhClear = [&](const Expected<Name>& identity, const AStringView label, const Core::GpuGraphResourceId destination, const u32 clearValue)->Expected<Core::GpuTaskId>{
        if(!identity)
            return MakeUnexpected(Failure{});
        Core::GpuTaskDesc clearDesc;
        clearDesc.setIdentity(*identity).setMarkerLabel(label).setScheduling(clearScheduling).setDependencies({ &buildDependency, 1u });
        return m_graph.addClearBufferTask(clearDesc, Core::GpuClearBufferTaskDesc{ .destination = destination, .clearValue = clearValue, });
    };
    const auto appendPureSoftwareBvhClear = [&](const Expected<Core::GpuTaskId>& clearTaskResult)noexcept{
        if(!clearTaskResult)
            return false;
        const Core::GpuTaskId clearTask = *clearTaskResult;
        if(!firstTask.valid())
            firstTask = clearTask;
        buildDependency = clearTask;
        return true;
    };

    for(const PreparedMeshSwBvhGraphResources& resources : pureSoftwareMeshSwBvhGraphResources){
        const PreparedMeshSwBvhBuild& build = resources.build;
        if(!build.performRefit){
            if(!appendPureSoftwareBvhClear(addPureSoftwareBvhClear(DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_keys_clear")), "Shadow Prepare SW-BVH Sort-Key Clear", resources.sortKeys, BvhNodeIndex::Invalid))
                || !appendPureSoftwareBvhClear(addPureSoftwareBvhClear(DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_parent_clear")), "Shadow Prepare SW-BVH Parent Clear", resources.parent, BvhNodeIndex::Invalid))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH sentinel clears"));
                return false;
            }
        }
        if(!appendPureSoftwareBvhClear(addPureSoftwareBvhClear(DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_counter_clear")), "Shadow Prepare SW-BVH Counter Clear", resources.visitCounter, 0u))){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH counter clear"));
            return false;
        }

        const Array<Core::GpuTaskResourceUse, 7u> buildResourceUses{
            ReadUse(resources.position, Core::ResourceStates::ShaderResource),
            ReadUse(resources.triangleIndex, Core::ResourceStates::ShaderResource),
            ReadWriteUse(resources.node, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(resources.parent, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(resources.sortKeys, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(resources.sortPayload, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(resources.visitCounter, Core::ResourceStates::UnorderedAccess),
        };
        const auto buildIdentity = DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_build"));
        if(!buildIdentity){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH build callback"));
            return false;
        }
        Core::GpuTaskDesc buildDesc;
        buildDesc.setIdentity(*buildIdentity).setMarkerLabel("Shadow Prepare SW-BVH Build").setScheduling(buildScheduling).setDependencies({ &buildDependency, 1u }).setResourceUses(buildResourceUses);
        const auto buildTaskResult = m_graph.addTask<
            ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask
        >(buildDesc, ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask::Payload{ .raytracingSystem = m_raytracingSystem, .timingTicket = timingTicket, .build = build, });
        if(!buildTaskResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH build callback"));
            return false;
        }
        const Core::GpuTaskId buildTask = *buildTaskResult;
        buildDependency = buildTask;
        lastTask = buildTask;
    }
    if(!firstTask.valid() || !lastTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: pure software BVH graph chain has no task bounds"));
        return false;
    }
    shadowPrepareDependency = buildDependency;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPrepareGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    payload.outcome.ready = false;
    // The graph establishes the selector ConstantBuffer state before recording.
    if(!payload.targets.bindless.valid())
        return false;
    const auto shadowResourcesPrepared = payload.raytracingSystem.recordPreflightShadowVisibilityResources(
        commandList,
        payload.targets,
        payload.sceneTlasBuildGraphOwned,
        payload.meshBlasBuildsGraphOwned,
        payload.meshBlasGeometryBuildInputStatesGraphOwned,
        payload.meshSwBvhBuildsGraphOwned,
        payload.preparedMeshSwBvhBuildsRecordedByGraph
    );

    // Selector upload precedes this task; compiler establishes ConstantBuffer state first.
    if(!shadowResourcesPrepared)
        return false;
    payload.outcome.ready = *shadowResourcesPrepared;

    // These declarations export BLAS/SW-BVH boundary states before the Prefix packet seeds.
    return true;
}


void ShadowPrepareGraphTask::Accepted(Payload& payload, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
    if(payload.currentBindlessSlotsGraphOwned)
        payload.targets.bindless.slotsUploaded = true;
    payload.raytracingSystem.confirmPreparedShadowTraceGeometryNormalization();
    if(payload.shadowMaterialContextBatchGraphOwned)
        payload.raytracingSystem.confirmPreparedShadowMaterialContextUploads();
    if(payload.sceneBvhBatchGraphOwned)
        payload.raytracingSystem.confirmPreparedSceneBvhUploads();
}


void ShadowPrepareGraphTask::Discarded(Payload& payload)noexcept{
    payload.timingTicket.discard();

    // Failed preparation keeps storage but invalidates the frame plan and caches.
    payload.outcome.ready = false;
    payload.outcome.resourcesValid = false;
    payload.targets.bindless.slotsUploaded = payload.deferredBindlessSlotsWereUploaded;
    payload.raytracingSystem.discardPreflightShadowVisibilityResources();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPrepareSoftwareBvhBuildGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    return payload.raytracingSystem.recordPreparedMeshSwBvhBuildAfterGraphClears(commandList, payload.build);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_trace_geometry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshSnapshot = ECSRenderDetail::MeshRayTracingResourceSnapshot;
using BufferRecord = ShadowTraceBufferRecord;
inline constexpr Array<Core::BufferHandle MeshSnapshot::*, s_ShadowTraceMemberCount> s_BufferMembers = { &MeshSnapshot::positionBuffer, &MeshSnapshot::triangleIndexBuffer, &MeshSnapshot::attributeBuffer, &MeshSnapshot::swBvhNodeBuffer, };

class BufferIndex final : NoCopy{
private:
    struct InlineEntry{
        Core::Buffer* buffer = nullptr;
        BufferRecord record;
    };


private:
    using IndexMap = HashMap<Core::Buffer*, BufferRecord, Core::Alloc::ScratchArena>;


public:
    BufferIndex(Core::Alloc::ScratchArena& scratchArena, const usize maximumBucketCount)noexcept
        : m_scratchArena(scratchArena)
        , m_maximumBucketCount(maximumBucketCount)
    {}


public:
    void add(Core::Buffer* const buffer, const u32 memberIndex, const usize meshIndex){
        BufferRecord& record = findOrAppend(buffer);
        if(record.firstMesh[memberIndex] == s_ShadowTraceMissingIndex)
            record.firstMesh[memberIndex] = meshIndex;
    }

    [[nodiscard]] BufferRecord* find(Core::Buffer* const buffer)noexcept{
        if(m_map){
            const auto found = m_map->find(buffer);
            return found == m_map->end() ? nullptr : &found.value();
        }
        for(InlineEntry& entry : m_inlineEntries){
            if(entry.buffer == buffer)
                return &entry.record;
        }
        return nullptr;
    }


private:
    [[nodiscard]] BufferRecord& findOrAppend(Core::Buffer* const buffer){
        if(BufferRecord* const found = find(buffer))
            return *found;
        if(!m_map){
            if(m_inlineEntries.size() < s_ShadowTraceInlineCapacity){
                m_inlineEntries.push_back(InlineEntry{ .buffer = buffer, .record = {} });
                return m_inlineEntries.back().record;
            }
            m_map.emplace(m_maximumBucketCount, IndexMap::hasher{}, IndexMap::key_equal{}, IndexMap::allocator_type(m_scratchArena));
            for(const InlineEntry& entry : m_inlineEntries){
                if(!m_map->emplace(entry.buffer, entry.record).second)
                    TerminateInvariant();
            }
        }
        return m_map->try_emplace(buffer).first.value();
    }


private:
    FixedVector<InlineEntry, s_ShadowTraceInlineCapacity> m_inlineEntries;
    Optional<IndexMap> m_map;
    Core::Alloc::ScratchArena& m_scratchArena;
    usize m_maximumBucketCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FreezePreparedShadowTraceGeometryBuffers(
    const Span<const ECSRenderDetail::MeshRayTracingResourceSnapshot> meshes,
    const ShadowTraceGeometrySelection& selection,
    Core::Alloc::ScratchArena& scratchArena,
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& acceptedBuffers,
    PreparedShadowTraceGeometryBufferVector& outPrepared
){
    outPrepared.clear();
    using namespace __hidden_shadow_trace_geometry;
    constexpr usize s_BucketsPerMesh = LengthOf(s_BufferMembers) * 2u;
    if(MultiplyOverflows<usize>(meshes.size(), s_BucketsPerMesh))
        return false;

    BufferIndex bufferIndex(scratchArena, meshes.size() * s_BucketsPerMesh);
    for(usize meshIndex = 0u; meshIndex < meshes.size(); ++meshIndex){
        const MeshSnapshot& mesh = meshes[meshIndex];
        for(u32 memberIndex = 0u; memberIndex < LengthOf(s_BufferMembers); ++memberIndex)
            bufferIndex.add((mesh.*s_BufferMembers[memberIndex]).get(), memberIndex, meshIndex);
    }

    // Keep accepted invisible meshes; prune only resources whose mesh was removed.
    usize retainedCount = 0u;
    for(usize acceptedIndex = 0u; acceptedIndex < acceptedBuffers.size(); ++acceptedIndex){
        BufferRecord* const record = bufferIndex.find(acceptedBuffers[acceptedIndex].get());
        if(!record)
            continue;
        record->accepted = true;
        if(retainedCount != acceptedIndex)
            acceptedBuffers[retainedCount] = Move(acceptedBuffers[acceptedIndex]);
        ++retainedCount;
    }
    acceptedBuffers.resize(retainedCount);

    if(!selection.includeHardware && !selection.includeSoftware)
        return true;

    const auto appendBuffer = [&](const Core::BufferHandle& buffer, const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh, const AStringView identitySuffix, const u8 role){
        if(!buffer)
            return false;
        BufferRecord& record = *MakeNotNull(bufferIndex.find(buffer.get()));
        if(record.preparedIndex != s_ShadowTraceMissingIndex){
            outPrepared[record.preparedIndex].roles |= role;
            return true;
        }

        const auto identity = DeriveName(mesh.meshName, identitySuffix);
        if(!identity)
            return false;
        const b8 normalizedByAcceptedPacket = record.accepted;
        outPrepared.push_back(PreparedShadowTraceGeometryBuffer{
            .buffer = buffer,
            .identity = *identity,
            // Fresh buffers keep creation state; only normalized ones enter as ShaderResource.
            .initialState = normalizedByAcceptedPacket ? Core::ResourceStates::ShaderResource : buffer->getCreationDescription().initialState, .roles = role, .normalizationPending = !normalizedByAcceptedPacket,
        });
        record.preparedIndex = outPrepared.size() - 1u;
        return true;
    };
    const auto appendSelected = [&](const Span<const NotNull<Core::Buffer*>> selectedBuffers, const u32 memberIndex, const AStringView identitySuffix, const u8 role){
        for(const NotNull<Core::Buffer*> selectedBuffer : selectedBuffers){
            const BufferRecord* const record = bufferIndex.find(selectedBuffer.get());
            if(!record || record->firstMesh[memberIndex] == s_ShadowTraceMissingIndex)
                return false;
            const MeshSnapshot& mesh = meshes[record->firstMesh[memberIndex]];
            if(!appendBuffer(mesh.*s_BufferMembers[memberIndex], mesh, identitySuffix, role))
                return false;
        }
        return true;
    };
    // Keep off-screen rebuild inputs frozen so later frames never import stale states.
    const auto appendPendingBlasBuildInputs = [&]{
        for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh : meshes){
            if(!mesh.runtimeMesh && !mesh.blasBuildPending)
                continue;
            if(
                !appendBuffer(mesh.positionBuffer, mesh, s_ShadowTraceHardwarePositionIdentitySuffix, PreparedShadowTraceGeometryRole::HardwarePosition)
                || !appendBuffer(mesh.triangleIndexBuffer, mesh, s_ShadowTraceHardwareIndexIdentitySuffix, PreparedShadowTraceGeometryRole::HardwareIndex)
            )
                return false;
        }
        return true;
    };
    const auto appendPendingSwBvhBuildInputs = [&]{
        for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh : meshes){
            if(!mesh.runtimeMesh && !mesh.swBvhBuildPending)
                continue;
            if(
                !appendBuffer(mesh.swBvhNodeBuffer, mesh, s_ShadowTraceSoftwareNodeIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareNode)
                || !appendBuffer(mesh.positionBuffer, mesh, s_ShadowTraceSoftwarePositionIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwarePosition)
                || !appendBuffer(mesh.triangleIndexBuffer, mesh, s_ShadowTraceSoftwareIndexIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareIndex)
            )
                return false;
        }
        return true;
    };

    b8 collected = true;
    if(selection.includeHardware){
        collected =
            appendSelected(selection.hardwarePositions, s_ShadowTracePositionMember, s_ShadowTraceHardwarePositionIdentitySuffix, PreparedShadowTraceGeometryRole::HardwarePosition)
            && appendSelected(selection.hardwareIndices, s_ShadowTraceIndexMember, s_ShadowTraceHardwareIndexIdentitySuffix, PreparedShadowTraceGeometryRole::HardwareIndex)
            && appendSelected(selection.hardwareAttributes, s_ShadowTraceAttributeMember, s_ShadowTraceHardwareAttributeIdentitySuffix, PreparedShadowTraceGeometryRole::HardwareAttribute)
            && appendPendingBlasBuildInputs()
        ;
    }
    if(collected && selection.includeSoftware){
        collected =
            appendSelected(selection.softwareNodes, s_ShadowTraceNodeMember, s_ShadowTraceSoftwareNodeIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareNode)
            && appendSelected(selection.softwarePositions, s_ShadowTracePositionMember, s_ShadowTraceSoftwarePositionIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwarePosition)
            && appendSelected(selection.softwareIndices, s_ShadowTraceIndexMember, s_ShadowTraceSoftwareIndexIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareIndex)
            && appendSelected(selection.softwareAttributes, s_ShadowTraceAttributeMember, s_ShadowTraceSoftwareAttributeIdentitySuffix, PreparedShadowTraceGeometryRole::SoftwareAttribute)
            && appendPendingSwBvhBuildInputs()
        ;
    }
    if(!collected){
        outPrepared.clear();
        return false;
    }

    if(AddOverflows<usize>(acceptedBuffers.size(), outPrepared.size())){
        outPrepared.clear();
        return false;
    }
    const usize acceptedCapacity = acceptedBuffers.size() + outPrepared.size();
    if(acceptedCapacity > acceptedBuffers.max_size()){
        outPrepared.clear();
        return false;
    }
    // Acceptance publishes non-normalized handles; reserve the full union to stay allocation-free.
    acceptedBuffers.reserve(acceptedCapacity);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> ShadowMaterialContextPreparer::prepare(
    const PreparedShadowMaterialContextRoute::Enum route,
    const b8 staticScene,
    const usize sceneInstanceCount,
    const Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
    const Span<const InstanceGpuData> shadowInstanceData,
    MaterialTypedByteDataVector& shadowMaterialTypedBytes
){
    using namespace RayTracingSoftwareBvhDetail;

    if(route != PreparedShadowMaterialContextRoute::Hardware && route != PreparedShadowMaterialContextRoute::Software)
        return MakeUnexpected(Failure{});
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    const usize materialTypedUploadBytes = ECSRenderDetail::ResolveMaterialTypedUploadByteCount(shadowMaterialTypedBytes);
    const u64 materialContextHash = ComputeShadowMaterialContextHash(instanceMaterials, shadowInstanceData, shadowMaterialTypedBytes);
    const b8 hardware = route == PreparedShadowMaterialContextRoute::Hardware;
    const b8 canReuseMaterialContext =
        staticScene
        && (!hardware || !m_system.m_rayTracingState.m_sceneHasTransparentOccluder)
        && (hardware ? m_system.m_rayTracingState.m_hwShadowMaterialContextHashValid : m_system.m_rayTracingState.m_swShadowMaterialContextHashValid)
        && (hardware ? m_system.m_rayTracingState.m_hwShadowMaterialContextHash : m_system.m_rayTracingState.m_swShadowMaterialContextHash) == materialContextHash
        && HasPreparedShadowMaterialContextBuffers(m_system.m_rayTracingState, instanceMaterials.size(), shadowInstanceData.size(), materialTypedUploadBytes)
    ;
    if(!hardware && canReuseMaterialContext && !m_system.capturePreparedShadowMaterialContextCacheReuse(materialContextHash, instanceMaterials.size(), shadowInstanceData.size(), materialTypedUploadBytes))
        return MakeUnexpected(Failure{});
    if(!canReuseMaterialContext){
        if(
            !RayTraceMaterialResources(m_system.m_graphics, m_system.m_rayTracingState, m_system.m_rayTracingState).ensureShadowInstanceMaterialBuffer(sceneInstanceCount)
            || !RayTraceMaterialResources(m_system.m_graphics, m_system.m_rayTracingState, m_system.m_rayTracingState).ensureShadowInstanceContextBuffer(shadowInstanceData.size())
            || !RayTraceMaterialResources(m_system.m_graphics, m_system.m_rayTracingState, m_system.m_rayTracingState).ensureShadowMaterialTypedBuffer(materialTypedUploadBytes)
            || !HasPreparedShadowMaterialContextBuffers(m_system.m_rayTracingState, instanceMaterials.size(), shadowInstanceData.size(), materialTypedUploadBytes)
        )
            return MakeUnexpected(Failure{});
        if(!m_system.capturePreparedShadowMaterialContext(route, staticScene, materialContextHash, instanceMaterials, shadowInstanceData, Span<const u8>(shadowMaterialTypedBytes.data(), materialTypedUploadBytes)))
            return MakeUnexpected(Failure{});
    }
    return materialContextHash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityGraphCoordinator::declareDeferredShadowVisibilityTask(
    const ShadowVisibilityGraphContext& context,
    const DeferredLightingGraphResources& deferredLightingResources,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan,
    const RayTracingSceneGraphReads& hardwareSceneReads,
    const b8 hardwareShadowSupported,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId depth,
    const Core::GpuGraphResourceId shadowVisibility,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Span<const Core::GpuGraphResourceId> traceGeometryResources,
    const Core::GpuGraphResourceSetId traceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    const Core::GpuTaskId prefixTask,
    const Core::GpuExternalCompletionId laggedLightingHistoryWriterDrainCompletion,
    Core::Alloc::ScratchArena& scratchArena,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    b8& opaqueProduced,
    b8& transparentTraceProduced,
    u32& opaqueFrameIndex
){
    using namespace RendererTaskGraphDetail;

    const b8 hardwareTransparentTrace = rayTracingPlan.hardwareTransparentTrace;
    const b8 csgShadow = rayTracingPlan.lightSpace.ready && (rayTracingPlan.lightSpace.push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u;
    const b8 hardwareCsgShadow = hardwareShadowSupported && csgShadow;

    m_deferredShadowVisibilityOpaqueTask = {};
    m_deferredShadowVisibilityOpaqueFirstWaveletTask = {};
    m_deferredShadowVisibilityOpaqueResolveTask = {};
    m_deferredShadowCombinedUpsample = false;
    m_deferredShadowCombinedWavelet = false;
    m_deferredShadowCombinedTemporal = false;
    m_deferredShadowVisibilityTransparentTraceTask = {};
    m_deferredShadowVisibilityTransparentTemporalMergeTask = {};
    m_deferredShadowVisibilityTransparentFirstWaveletTask = {};
    m_deferredShadowVisibilityDiagnosticsTask = {};
    m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
    m_deferredShadowVisibilityAllLitClearTask = {};
    m_deferredShadowVisibilityTask = {};
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !worldPosition.valid()
        || !normal.valid()
        || !depth.valid()
        || !shadowVisibility.valid()
        || !currentBindlessSlots.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !prefixTask.valid()
        || (hardwareTransparentTrace && !hardwareSceneReads.valid())
    )
        return false;

    Array<Core::GpuTaskExternalStateSource, s_ShadowVisibilityStateSourceCapacity> shadowVisibilityStateSources = {};
    usize shadowVisibilityStateSourceCount = 0u;
    const auto* const shadowScratchStates = context.shadowComputeState.source();
    if(shadowScratchStates)
        shadowVisibilityStateSources[shadowVisibilityStateSourceCount++] = { .states = shadowScratchStates, };
    const auto* const shadowReturnStates = context.shadowReturnState.source();
    if(shadowReturnStates)
        shadowVisibilityStateSources[shadowVisibilityStateSourceCount++] = { .states = shadowReturnStates, .applicableConsumerQueueClass = Core::CommandQueue::Compute, };
    const Core::GpuTaskExternalStateSource* const shadowVisibilityStateSourceData = shadowVisibilityStateSourceCount != 0u ? shadowVisibilityStateSources.data() : nullptr;

    const Core::GpuExternalCompletionId* const laggedLightingHistoryWriterDrainDependencies = laggedLightingHistoryWriterDrainCompletion.valid() ? &laggedLightingHistoryWriterDrainCompletion : nullptr;
    const usize laggedLightingHistoryWriterDrainDependencyCount = laggedLightingHistoryWriterDrainCompletion.valid() ? 1u : 0u;

    opaqueProduced = false;
    transparentTraceProduced = false;
    opaqueFrameIndex = 0u;
    // Only the prepared route exposes this boundary; other paths keep the monolithic Shadow Visibility callback.
    const b8 preparedSoftTransparentFoldCandidate =
        rayTracingPlan.softTransparentFoldReady
        && deferredTargets.shadowCoarseTransmittance
        && deferredTargets.shadowSoftHalfA
        && deferredTargets.shadowSoftHalfB
        && deferredTargets.shadowSoftGeometry
        && deferredTargets.shadowSoftGeometryPrev
        && deferredTargets.shadowHistA
        && deferredTargets.shadowHistB
        && deferredTargets.shadowMomentsA
        && deferredTargets.shadowMomentsB
        && deferredTargets.transparentSoftHalf
        && deferredTargets.transparentHistA
        && deferredTargets.transparentHistB
        && deferredTargets.transparentMomentsA
        && deferredTargets.transparentMomentsB
        && materialContextSlots.valid()
        && !traceGeometryResources.empty()
    ;
    const b8 splitSoftTransparentFold = preparedSoftTransparentFoldCandidate;
    const b8 combinedSoftUpsample = splitSoftTransparentFold && rayTracingPlan.combinedSoftUpsample;
    m_deferredShadowCombinedUpsample = combinedSoftUpsample;
    const b8 combinedSoftWavelet = CanCombineSoftShadowWavelets(
        combinedSoftUpsample,
        rayTracingPlan.combinedSoftWaveletReady,
        rayTracingPlan.opaqueTemporalMergeReady,
        rayTracingPlan.transparentTemporalMergeReady,
        NWB_SHADOW_RESOLVE_PASS_COUNT,
        NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT
    );
    m_deferredShadowCombinedWavelet = combinedSoftWavelet;
    const b8 combinedSoftTemporal = combinedSoftWavelet && rayTracingPlan.combinedSoftTemporalReady;
    m_deferredShadowCombinedTemporal = combinedSoftTemporal;
    // Adaptive fallback stays in the monolithic callback, but its primitives and diagnostic lifecycle are fixed by this route.
    // A frame with no clear/copy work still owns its tick through the semantic task's accepted hook without gaining empty graph nodes.
    const b8 graphOwnedAdaptiveCandidate = !splitSoftTransparentFold && rayTracingPlan.adaptivePlan.enabled;
    GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan = graphOwnedAdaptiveCandidate ? rayTracingPlan.adaptivePlan : GraphOwnedAdaptiveShadowPlan{};
    // Merge pipeline may be ready before history has an accepted frame. Bootstrap publishes the pair, but only usable history samples prior geometry.
    const b8 softShadowHistoryReadable = rayTracingPlan.softShadowHistoryReadable;
    // Opaque merge runs before the transparent tail and shares its selector. Freeze its pair while the packet owns the temporal route.
    const b8 graphOwnsOpaqueTemporalMergeEntryStates = splitSoftTransparentFold && rayTracingPlan.opaqueTemporalMergeReady;
    const b8 opaqueHistoryFrontIsA = rayTracingPlan.historyFrontIsA;

    const b8 traceGeometryStatesGraphOwned = traceGeometrySet.valid();
    ShadowVisibilityResourcePlan shadowResourcePlan(scratchArena);
    const ShadowVisibilityResourceInputs shadowResourceInputs{
        deferredTargets, rayTracingShadowResources, rayTracingResources, rayTracingPlan, hardwareSceneReads,
        worldPosition, normal, depth, shadowVisibility, currentBindlessSlots, sceneShading, lights, materialContextSlots,
        traceGeometryResources, traceGeometryStatesGraphOwned, splitSoftTransparentFold, graphOwnsOpaqueTemporalMergeEntryStates,
        softShadowHistoryReadable, opaqueHistoryFrontIsA, hardwareTransparentTrace, graphOwnedAdaptivePlan.compact
    };
    ShadowVisibilityResourcePlanner shadowResourcePlanner(context.graph, context.rayTracing);
    if(!shadowResourcePlanner.prepare(shadowResourceInputs, shadowResourcePlan))
        return false;
    const auto& sceneGeometryDomain = shadowResourcePlan.sceneGeometryDomain;
    const auto& sceneTlas = shadowResourcePlan.sceneTlas;
    const auto& hardwareTransparentScratch = shadowResourcePlan.hardwareTransparentScratch;
    const auto& adaptiveEdgeCounter = shadowResourcePlan.adaptiveEdgeCounter;
    const auto& resourceUses = shadowResourcePlan.resourceUses;

    const RendererFramePipelineDetail::TraceResourceSetUses traceResourceSets = RendererFramePipelineDetail::MakeTraceResourceSetUses(traceGeometrySet, traceMaterialSampledTextureSet);
    const Core::GpuTaskResourceSetUse traceGeometrySetUse = traceResourceSets.useCount != 0u ? traceResourceSets.uses[0u] : Core::GpuTaskResourceSetUse{};

    if(splitSoftTransparentFold){
        // The prepared soft path keeps the opaque first wavelet, resolve tail, transparent trace, and temporal/RGB resolve as separate callbacks.
        const SoftShadowSplitRoute splitRoute{
            .hardwareShadowSupported = hardwareShadowSupported,
            .hardwareCsgShadow = hardwareCsgShadow,
            .csgShadow = csgShadow,
            .traceGeometryStatesGraphOwned = traceGeometryStatesGraphOwned,
            .combinedSoftUpsample = combinedSoftUpsample,
            .combinedSoftWavelet = combinedSoftWavelet,
            .combinedSoftTemporal = combinedSoftTemporal,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
        };
        const SoftShadowSplitResourceInputs splitResourceInputs{
            .targets = deferredTargets,
            .rayTracingResources = rayTracingResources,
            .rayTracingPlan = rayTracingPlan,
            .hardwareSceneReads = hardwareSceneReads,
            .hardwareTransparentScratch = hardwareTransparentScratch,
            .route = splitRoute,
            .traceGeometryResources = traceGeometryResources,
            .worldPosition = worldPosition,
            .normal = normal,
            .depth = depth,
            .shadowVisibility = shadowVisibility,
            .currentBindlessSlots = currentBindlessSlots,
            .sceneShading = sceneShading,
            .lights = lights,
            .materialContextSlots = materialContextSlots,
            .sceneGeometryDomain = sceneGeometryDomain,
            .sceneTlas = sceneTlas,
        };
        SoftShadowSplitResourceUses splitResourceUses(scratchArena);
        SoftShadowSplitResourceUseBuilder resourceUseBuilder(context.graph);
        if(!resourceUseBuilder.build(splitResourceInputs, splitResourceUses))
            return false;

        const SoftShadowSplitTaskChainInputs taskChainInputs{
            .targets = deferredTargets,
            .deferredLightingResources = deferredLightingResources,
            .rayTracingResources = rayTracingResources,
            .rayTracingPlan = rayTracingPlan,
            .shadowPrepared = context.shadowPrepared,
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueResolveTiming = opaqueResolveTiming,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .route = splitRoute,
            .traceGeometryResources = traceGeometryResources,
            .traceResourceSetUses = Span<const Core::GpuTaskResourceSetUse>(traceResourceSets.uses.data(), traceResourceSets.useCount),
            .traceGeometrySetUse = traceGeometrySetUse,
            .stateSources = Span<const Core::GpuTaskExternalStateSource>(shadowVisibilityStateSources.data(), shadowVisibilityStateSourceCount),
            .historyWriterDrainDependencies = Span<const Core::GpuExternalCompletionId>(laggedLightingHistoryWriterDrainDependencies, laggedLightingHistoryWriterDrainDependencyCount),
            .currentBindlessSlots = currentBindlessSlots,
            .sceneShading = sceneShading,
            .lights = lights,
            .materialContextSlots = materialContextSlots,
            .prefixTask = prefixTask,
        };
        SoftShadowSplitTasks splitTasks;
        SoftShadowSplitTaskChainBuilder taskChainBuilder(context.graph, context.graphics, context.arena, context.rayTracing);
        const b8 declared = taskChainBuilder.declare(taskChainInputs, scratchArena, splitResourceUses, splitTasks);
        m_deferredShadowVisibilityOpaqueTask = splitTasks.opaque;
        m_deferredShadowVisibilityOpaqueFirstWaveletTask = splitTasks.opaqueFirstWavelet;
        m_deferredShadowVisibilityOpaqueResolveTask = splitTasks.opaqueResolve;
        m_deferredShadowVisibilityTransparentTraceTask = splitTasks.transparentTrace;
        m_deferredShadowVisibilityTransparentTemporalMergeTask = splitTasks.transparentTemporalMerge;
        m_deferredShadowVisibilityTransparentFirstWaveletTask = splitTasks.transparentFirstWavelet;
        m_deferredShadowVisibilityTask = splitTasks.fold;
        m_deferredShadowVisibilityDiagnosticsTask = splitTasks.diagnostics;
        return declared;
    }

    Core::GpuTaskId shadowVisibilityDependency = prefixTask;
    b8 adaptivePrimitivePrecedesVisibility = false;
    if(graphOwnedAdaptivePlan.enabled){
        // The append counter is private adaptive scratch.
        // The compiler owns its CopyDest->UAV handoff before the Shadow Visibility callback, which still gates the adaptive producer.
        Core::GpuTaskSchedulingHint primitiveScheduling;
        primitiveScheduling.cost = Core::GpuTaskCostHint::Tiny;
        primitiveScheduling.forceSubmissionBoundary = false;
        primitiveScheduling.allowPacketMerge = true;
        EnableSameFamilyComputeEffectRouting(primitiveScheduling, false);
        EnableCrossFamilyComputeEffectRouting(primitiveScheduling);

        if(graphOwnedAdaptivePlan.compact){
            Core::GpuTaskSchedulingHint counterClearScheduling = primitiveScheduling;
            counterClearScheduling.mergeWithPrevious = adaptivePrimitivePrecedesVisibility;
            EnableSameFamilyComputeEffectRouting(counterClearScheduling, adaptivePrimitivePrecedesVisibility);
            Core::GpuTaskDesc counterClearDesc;
            counterClearDesc.setIdentity(Name(s_ShadowPassAdaptiveCounterClear)).setMarkerLabel("Shadow Adaptive Counter Clear").setScheduling(counterClearScheduling).setDependencies({ &shadowVisibilityDependency, 1u });
            const auto deferredShadowVisibilityAdaptiveCounterClearTaskResult = context.graph.addClearBufferTask(counterClearDesc, Core::GpuClearBufferTaskDesc{ .destination = adaptiveEdgeCounter, .clearValue = 0u, });
            if(!deferredShadowVisibilityAdaptiveCounterClearTaskResult){
                m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned adaptive shadow counter clear"));
                return false;
            }
            m_deferredShadowVisibilityAdaptiveCounterClearTask = *deferredShadowVisibilityAdaptiveCounterClearTaskResult;
            shadowVisibilityDependency = m_deferredShadowVisibilityAdaptiveCounterClearTask;
            adaptivePrimitivePrecedesVisibility = true;
        }
    }

    // Preparedness is established while Shadow Prepare records, after this graph is declared.
    // The monolithic path opens with an all-lit clear: real producers overwrite it, a missing one keeps white with no state bridge.
    // Keep this Compute-native clear on the selected Compute packet with Shadow Visibility.
    Core::GpuTaskSchedulingHint allLitClearScheduling;
    allLitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    allLitClearScheduling.forceSubmissionBoundary = false;
    allLitClearScheduling.allowPacketMerge = true;
    allLitClearScheduling.mergeWithPrevious = adaptivePrimitivePrecedesVisibility;
    EnableSameFamilyComputeEffectRouting(allLitClearScheduling, adaptivePrimitivePrecedesVisibility);
    EnableCrossFamilyComputeEffectRouting(allLitClearScheduling);
    const Core::GpuTaskResourceUse allLitClearResourceUse = WriteTextureUse(shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::CopyDest);
    Core::GpuTaskDesc allLitClearDesc;
    allLitClearDesc
        .setIdentity(Name(s_ShadowPassAllLitClear))
        .setMarkerLabel("Shadow Visibility All-Lit Clear")
        .setScheduling(allLitClearScheduling)
        .setDependencies({ &shadowVisibilityDependency, 1u })
        .setExternalDependencies({ laggedLightingHistoryWriterDrainDependencies, laggedLightingHistoryWriterDrainDependencyCount })
        .setResourceUses({ &allLitClearResourceUse, 1u })
    ;
    const auto deferredShadowVisibilityAllLitClearTaskResult = context.graph.addTask<
        ECSRenderDetail::FloatTextureClearGraphTask
    >(allLitClearDesc, ECSRenderDetail::FloatTextureClearGraphTask::Payload{ .destination = shadowVisibility, .subresources = ECSRenderDetail::s_ShadowVisibilitySubresources, .floatValue = ECSRenderDetail::s_ShadowVisibilityAllLitClearColor, });
    if(!deferredShadowVisibilityAllLitClearTaskResult){
        m_deferredShadowVisibilityAllLitClearTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned all-lit shadow-visibility clear"));
        return false;
    }
    m_deferredShadowVisibilityAllLitClearTask = *deferredShadowVisibilityAllLitClearTaskResult;
    shadowVisibilityDependency = m_deferredShadowVisibilityAllLitClearTask;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    EnableSameFamilyComputeEffectRouting(scheduling);
    EnableCrossFamilyComputeEffectRouting(scheduling);
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name(s_ShadowVisibilityPass))
        .setMarkerLabel(RendererGpuPassText::s_ShadowVisibilityLabel)
        .setScheduling(scheduling)
        .setDependencies({ &shadowVisibilityDependency, 1u })
        .setExternalStateSources({ shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ traceResourceSets.useCount != 0u ? traceResourceSets.uses.data() : nullptr, traceResourceSets.useCount })
    ;
    const auto deferredShadowVisibilityTaskResult = context.rayTracing.declareShadowVisibilityTask(
        context.graph,
        desc,
        deferredLightingResources,
        context.shadowPrepared,
        hardwareShadowSupported,
        graphOwnedAdaptivePlan,
        deferredTargets,
        timingTicket
    );
    if(!deferredShadowVisibilityTaskResult){
        m_deferredShadowVisibilityTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred shadow-visibility graph task"));
        return false;
    }
    m_deferredShadowVisibilityTask = *deferredShadowVisibilityTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PreparedShadowVisibilityTasksSharePacket(const Core::GpuCompiledGraph::ReadView& compiledPlan, const PreparedShadowVisibilityTasks& tasks)noexcept{
    if(tasks.combinedTemporal && (!tasks.combinedWavelet || tasks.opaqueFirstWavelet.valid()))
        return false;
    if(tasks.combinedWavelet && (!tasks.combinedUpsample || !tasks.transparentTemporalMerge.valid()))
        return false;
    if(!tasks.opaque.valid())
        return !tasks.combinedUpsample && !tasks.combinedWavelet && !tasks.combinedTemporal && !tasks.diagnostics.valid();
    if(tasks.combinedUpsample ? tasks.opaqueResolve.valid() : !tasks.opaqueResolve.valid())
        return false;
    const Array<Core::GpuTaskId, 3> required = { tasks.opaque, tasks.transparentTrace, tasks.transparentFirstWavelet };
    for(const Core::GpuTaskId task : required){
        if(!task.valid() || !compiledPlan.tasksSharePacket(tasks.terminal, task))
            return false;
    }
    if(!tasks.combinedTemporal && (!tasks.opaqueFirstWavelet.valid() || !compiledPlan.tasksSharePacket(tasks.terminal, tasks.opaqueFirstWavelet)))
        return false;
    if(tasks.opaqueResolve.valid() && !compiledPlan.tasksSharePacket(tasks.terminal, tasks.opaqueResolve))
        return false;
    if(tasks.diagnostics.valid() && !compiledPlan.tasksSharePacket(tasks.terminal, tasks.diagnostics))
        return false;
    return !tasks.transparentTemporalMerge.valid() || compiledPlan.tasksSharePacket(tasks.terminal, tasks.transparentTemporalMerge);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ShadowVisibilityMergeValidator::validate(const Core::GpuCompiledGraph::ReadView& compiledPlan, ShadowVisibilityMergeValidationResult& outResult)const noexcept{
    const RendererFramePipeline& pipeline = m_pipeline;
    outResult = ShadowVisibilityMergeValidationResult{};
    PreparedShadowVisibilityTasks tasks;
    tasks.terminal = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask;
    tasks.opaque = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueTask;
    tasks.opaqueFirstWavelet = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueFirstWaveletTask;
    tasks.opaqueResolve = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueResolveTask;
    tasks.transparentTrace = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTraceTask;
    tasks.transparentTemporalMerge = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentTemporalMergeTask;
    tasks.transparentFirstWavelet = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTransparentFirstWaveletTask;
    tasks.diagnostics = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityDiagnosticsTask;
    tasks.combinedUpsample = pipeline.m_shadowVisibilityGraph.m_deferredShadowCombinedUpsample;
    tasks.combinedWavelet = pipeline.m_shadowVisibilityGraph.m_deferredShadowCombinedWavelet;
    tasks.combinedTemporal = pipeline.m_shadowVisibilityGraph.m_deferredShadowCombinedTemporal;
    outResult.preparedTasksMerged = PreparedShadowVisibilityTasksSharePacket(compiledPlan, tasks);
    // Keep the all-lit clear in its packet; split-soft frames keep native clear.
    outResult.allLitClearMerged = pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityOpaqueTask.valid()
        ? !pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAllLitClearTask.valid()
        : pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAllLitClearTask.valid()
            && compiledPlan.tasksSharePacket(pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask, pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAllLitClearTask)
    ;
    // The append-counter clear must precede visibility in the same packet.
    outResult.adaptivePrimitivesMerged = !pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAdaptiveCounterClearTask.valid() || compiledPlan.tasksSharePacket(pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityTask, pipeline.m_shadowVisibilityGraph.m_deferredShadowVisibilityAdaptiveCounterClearTask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityResourcePlanner::prepare(const ShadowVisibilityResourceInputs& inputs, ShadowVisibilityResourcePlan& result){
    using namespace RendererTaskGraphDetail;
    const auto& deferredTargets = inputs.deferredTargets;
    const auto& rayTracingShadowResources = inputs.rayTracingShadowResources;
    const auto& rayTracingResources = inputs.rayTracingResources;
    const auto& rayTracingPlan = inputs.rayTracingPlan;
    const auto& hardwareSceneReads = inputs.hardwareSceneReads;
    const auto worldPosition = inputs.worldPosition;
    const auto normal = inputs.normal;
    const auto depth = inputs.depth;
    const auto shadowVisibility = inputs.shadowVisibility;
    const auto currentBindlessSlots = inputs.currentBindlessSlots;
    const auto sceneShading = inputs.sceneShading;
    const auto lights = inputs.lights;
    const auto materialContextSlots = inputs.materialContextSlots;
    const auto& traceGeometryResources = inputs.traceGeometryResources;
    const auto traceGeometryStatesGraphOwned = inputs.traceGeometryStatesGraphOwned;
    const auto splitSoftTransparentFold = inputs.splitSoftTransparentFold;
    const auto graphOwnsOpaqueTemporalMergeEntryStates = inputs.graphOwnsOpaqueTemporalMergeEntryStates;
    const auto softShadowHistoryReadable = inputs.softShadowHistoryReadable;
    const auto opaqueHistoryFrontIsA = inputs.opaqueHistoryFrontIsA;
    const auto hardwareTransparentTrace = inputs.hardwareTransparentTrace;
    const auto adaptiveCompact = inputs.adaptiveCompact;
    auto& sceneGeometryDomain = result.sceneGeometryDomain;
    auto& sceneTlas = result.sceneTlas;
    auto& hardwareTransparentScratch = result.hardwareTransparentScratch;
    auto& adaptiveEdgeCounter = result.adaptiveEdgeCounter;
    auto& resourceUses = result.resourceUses;

    const auto sceneGeometryDomainResult = m_graph.importHazardDomain(HazardDomainDesc(Name(s_ShadowPassSceneGeometry), s_SceneGeometryDomainLabel));
    if(!sceneGeometryDomainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred shadow-visibility graph resources"));
        return false;
    }
    sceneGeometryDomain = *sceneGeometryDomainResult;

    resourceUses.reserve(s_ShadowVisibilityResourceUseCapacity + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResources.size()));
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    // Shadow visibility samples the bindless depth image, so its declared layout must match the native shader read.
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // The split opaque producer overwrites visibility; the software monolith also folds transparency in place.
    resourceUses.push_back(splitSoftTransparentFold ? WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess) : ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const auto resourceResult = ImportExternalTexture(m_graph, texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalOpaqueTemporalHistoryTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label, const b8 isInput){
        if(!texture)
            return true;
        const auto resourceResult = ImportExternalTexture(m_graph, texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        if(graphOwnsOpaqueTemporalMergeEntryStates)
            resourceUses.push_back(isInput ? ReadUse(resource, Core::ResourceStates::ShaderResource) : WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        else if(!splitSoftTransparentFold){
            if(isInput){
                if(softShadowHistoryReadable)
                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
            }
            else if(rayTracingPlan.opaqueTemporalMergeReady)
                resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        }
        else
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalOpaqueTemporalStaticReadTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const auto resourceResult = ImportExternalTexture(m_graph, texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        if(softShadowHistoryReadable)
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        else if(splitSoftTransparentFold && !graphOwnsOpaqueTemporalMergeEntryStates)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalTransparentTemporalHistoryTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label, const b8 isInput){
        if(!texture)
            return true;
        const auto resourceResult = ImportExternalTexture(m_graph, texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        if(rayTracingPlan.transparentTemporalMergeReady){
            if(isInput){
                if(softShadowHistoryReadable)
                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
            }
            else
                resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        }
        return true;
    };
    const auto appendOptionalReadWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const auto resourceResult = ImportExternalBuffer(m_graph, buffer, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        resourceUses.push_back(ReadWriteUse(resource, state));
        return true;
    };
    b8 optionalResourcesImported =
        (
            // The monolith picks adaptive fallback after runtime slot checks. Declare coarse across that route; split callbacks omit it.
            splitSoftTransparentFold
            || appendOptionalWriteTexture(deferredTargets.shadowCoarseTransmittance, Name(s_ShadowPassCoarseTransmittance), "Shadow Coarse Transmittance")
        )
        // The retained callback first overwrites these scratch targets, then resolves them locally within the same command list.
        // Its graph entry must therefore preserve Unknown -> UAV first writes rather than fabricate a ReadWrite native source for a fresh target generation.
        && appendOptionalWriteTexture(deferredTargets.shadowSoftHalfA, Name(s_ShadowSoftHalfAName), s_ShadowSoftHalfALabel)
        && appendOptionalWriteTexture(deferredTargets.shadowSoftHalfB, Name(s_ShadowSoftHalfBName), s_ShadowSoftHalfBLabel)
        && appendOptionalWriteTexture(deferredTargets.shadowSoftGeometry, Name(s_ShadowSoftGeometryName), s_ShadowSoftGeometryLabel)
        && appendOptionalOpaqueTemporalStaticReadTexture(deferredTargets.shadowSoftGeometryPrev, Name(s_ShadowSoftGeometryPreviousName), s_ShadowSoftGeometryPreviousLabel)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowHistA, Name(s_ShadowHistoryAName), s_ShadowHistoryALabel, opaqueHistoryFrontIsA)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowHistB, Name(s_ShadowHistoryBName), s_ShadowHistoryBLabel, !opaqueHistoryFrontIsA)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowMomentsA, Name(s_ShadowMomentsAName), s_ShadowMomentsALabel, opaqueHistoryFrontIsA)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowMomentsB, Name(s_ShadowMomentsBName), s_ShadowMomentsBLabel, !opaqueHistoryFrontIsA)
        && appendOptionalWriteTexture(deferredTargets.transparentSoftHalf, Name(s_TransparentSoftHalfName), s_TransparentSoftHalfLabel)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentHistA, Name(s_TransparentHistoryAName), s_TransparentHistoryALabel, opaqueHistoryFrontIsA)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentHistB, Name(s_TransparentHistoryBName), s_TransparentHistoryBLabel, !opaqueHistoryFrontIsA)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentMomentsA, Name(s_TransparentMomentsAName), s_TransparentMomentsALabel, opaqueHistoryFrontIsA)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentMomentsB, Name(s_TransparentMomentsBName), s_TransparentMomentsBLabel, !opaqueHistoryFrontIsA)
        && (hardwareTransparentTrace || RendererFramePipelineDetail::AppendRayTracingSceneShadowBuffers(
            rayTracingResources,
            m_graph,
            resourceUses
        ))
        && appendOptionalReadWriteBuffer(rayTracingShadowResources.swShadowEdgeCounterBuffer, Name(s_ShadowEdgeCounterName), s_ShadowEdgeCounterLabel, Core::ResourceStates::UnorderedAccess)
        && appendOptionalReadWriteBuffer(rayTracingShadowResources.swShadowEdgeListBuffer, Name(s_ShadowPassEdgeList), "Shadow Edge List", Core::ResourceStates::UnorderedAccess)
        && appendOptionalReadWriteBuffer(rayTracingShadowResources.swShadowIndirectArgsBuffer, Name(s_ShadowPassIndirectArgs), "Shadow Indirect Arguments", Core::ResourceStates::UnorderedAccess)
    ;
    if(rayTracingShadowResources.sceneTlas){
        const auto sceneTlasResult = m_graph.importAccelStruct(
            rayTracingShadowResources.sceneTlas,
            AccelStructResourceDesc(Name(s_SceneTlasResourceName), s_SceneTlasResourceLabel)
                .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
        );
        if(sceneTlasResult)
            sceneTlas = *sceneTlasResult;
        optionalResourcesImported = optionalResourcesImported && sceneTlasResult.has_value();
        if(sceneTlas.valid() && !hardwareTransparentTrace)
            resourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
    }
    if(hardwareTransparentTrace){
        const Array<ReferenceWrapper<const Core::BufferHandle>, 3u> buffers = { rayTracingResources.hardwareTransparentCrossingsBuffer, rayTracingResources.hardwareTransparentOverflowListBuffer, rayTracingResources.hardwareTransparentOverflowArgsBuffer, };
        for(usize index = 0u; index < buffers.size(); ++index){
            const Core::BufferHandle& buffer = buffers[index].get();
            if(!buffer)
                return false;
            const Core::BufferDesc& bufferDesc = buffer->getCreationDescription();
            if(!bufferDesc.debugName)
                return false;
            const auto hardwareTransparentScratchResult = m_graph.importBuffer(buffer, BufferResourceDesc(bufferDesc.debugName, "Hardware Transparent Shadow Scratch").setInitialState(bufferDesc.initialState));
            if(!hardwareTransparentScratchResult){
                hardwareTransparentScratch[index] = {};
                return false;
            }
            hardwareTransparentScratch[index] = *hardwareTransparentScratchResult;
            resourceUses.push_back(ReadWriteUse(hardwareTransparentScratch[index], Core::ResourceStates::UnorderedAccess));
        }
        for(const Core::GpuTaskResourceUse& use : hardwareSceneReads.uses)
            resourceUses.push_back(use);
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred shadow-visibility dynamic resource"));
        return false;
    }
    if(adaptiveCompact){
        const auto adaptiveEdgeCounterResult = ImportExternalBuffer(m_graph, rayTracingShadowResources.swShadowEdgeCounterBuffer, Name(s_ShadowEdgeCounterName), s_ShadowEdgeCounterLabel);
        if(!adaptiveEdgeCounterResult){
            adaptiveEdgeCounter = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graph-owned adaptive shadow counter resource"));
            return false;
        }
        adaptiveEdgeCounter = *adaptiveEdgeCounterResult;
    }
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    if(materialContextSlots.valid() && !hardwareTransparentTrace)
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(!traceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : traceGeometryResources){
            if(!resource.valid())
                return false;
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityTargetBuilder::build(DeferredFrameTargets& targets){
    if(targets.shadowReceiverFactor != s_ShadowReceiverFactorHalf && targets.shadowReceiverFactor != s_ShadowReceiverFactorQuarter)
        return false;
    // Deferred lighting always samples this per-slot transmittance target.
    targets.shadowVisibilityFormat = Core::Format::RGBA16_FLOAT;

    const auto createShadowTarget = [&](Core::TextureDesc desc, const Name& name, const tchar* const errorMessage, Core::TextureHandle& outTexture){
        desc.setName(name);
        auto result = m_graphics.createTexture(desc);
        if(!result){
            outTexture.reset();
            NWB_LOGGER_ERROR(errorMessage);
            return false;
        }
        outTexture = Move(*result);
        return true;
    };

    Core::TextureDesc visibilityDesc;
    visibilityDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowVisibilityFormat)
        .setIsUAV(true)
        // The lagged-history copy may run on Transfer after producers finish. Keep all three transports shareable.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    if(!createShadowTarget(visibilityDesc, "engine/shadow/visibility", NWB_TEXT("RendererSystem: failed to create shadow visibility target"), targets.shadowVisibility))
        return false;

    // Round up half-resolution scratch so odd extents retain coverage.
    targets.shadowCoarseTransmittanceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc coarseDesc;
    coarseDesc
        .setWidth(DivideUpWrapping(targets.width, NWB_SW_SHADOW_COARSE_FACTOR))
        .setHeight(DivideUpWrapping(targets.height, NWB_SW_SHADOW_COARSE_FACTOR))
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowCoarseTransmittanceFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    if(!createShadowTarget(coarseDesc, "engine/shadow/coarse_transmittance", NWB_TEXT("RendererSystem: failed to create shadow coarse transmittance target"), targets.shadowCoarseTransmittance))
        return false;

    // Ping-pong half-resolution soft visibility and geometry cache.
    targets.shadowSoftFormat = Core::Format::RGBA16_FLOAT;
    targets.shadowSoftGeometryFormat = Core::Format::RGBA16_FLOAT;
    const u32 softHalfWidth = DivideUpWrapping(targets.width, targets.shadowReceiverFactor);
    const u32 softHalfHeight = DivideUpWrapping(targets.height, targets.shadowReceiverFactor);

    Core::TextureDesc softHalfDesc;
    softHalfDesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowSoftFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    Core::TextureDesc softGeometryDesc;
    softGeometryDesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setFormat(targets.shadowSoftGeometryFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;

    // Recreated history must not reproject through the previous target's matrix.
    if(
        !createShadowTarget(softHalfDesc, "engine/shadow/soft_half_a", NWB_TEXT("RendererSystem: failed to create soft shadow half-A target"), targets.shadowSoftHalfA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/soft_half_b", NWB_TEXT("RendererSystem: failed to create soft shadow half-B target"), targets.shadowSoftHalfB)
        || !createShadowTarget(softGeometryDesc, "engine/shadow/soft_geometry", NWB_TEXT("RendererSystem: failed to create soft shadow geometry cache target"), targets.shadowSoftGeometry)
        || !createShadowTarget(softHalfDesc, "engine/shadow/hist_a", NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-A target"), targets.shadowHistA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/hist_b", NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-B target"), targets.shadowHistB)
        || !createShadowTarget(softHalfDesc, "engine/shadow/moments_a", NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-A target"), targets.shadowMomentsA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/moments_b", NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-B target"), targets.shadowMomentsB)
        || !createShadowTarget(softGeometryDesc, "engine/shadow/soft_geometry_prev", NWB_TEXT("RendererSystem: failed to create soft shadow previous-frame geometry cache target"), targets.shadowSoftGeometryPrev)
    )
        return false;

    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    m_rayTracingState.m_transparentShadowSamplingHistory.reset();
    ResetSoftShadowTemporalHistoryState(
        m_rayTracingState.m_softShadowTemporalSeeded,
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending,
        m_rayTracingState.m_prevWorldToClipValid,
        m_rayTracingState.m_softShadowHistoryFrontIsA
    );

    // Transparent temporal history shares geometry and the opaque history selector.
    if(
        !createShadowTarget(softHalfDesc, "engine/shadow/transparent_soft_half", NWB_TEXT("RendererSystem: failed to create soft transparent shadow half target"), targets.transparentSoftHalf)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_hist_a", NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-A target"), targets.transparentHistA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_hist_b", NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-B target"), targets.transparentHistB)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_moments_a", NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-A target"), targets.transparentMomentsA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_moments_b", NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-B target"), targets.transparentMomentsB)
    )
        return false;

    // All resolve allocations belong to this target generation. Verify both history selectors once, before publication.
    for(u32 selector = 0u; selector < s_ShadowHistorySelectorCount; ++selector){
        const b8 frontIsA = selector != 0u;
        const SoftShadowCombinedWaveletInputs waveletInputs{
            .opaqueHistory = frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get(),
            .opaqueMoments = frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get(),
            .transparentHistory = frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get(),
            .transparentMoments = frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get(),
            .geometry = targets.shadowSoftGeometry.get(),
            .opaqueOutput = targets.shadowSoftHalfB.get(),
            .transparentOutput = targets.shadowSoftHalfA.get(),
        };
        if(!waveletInputs.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: soft-shadow target allocation produced aliased resolve resources"));
            return false;
        }
    }

    // Edge records are capped at one per pixel; overflow falls back to interpolation.
    const Expected<u32> edgeListCapacityResult = TryResolveShadowEdgeListCapacityRecords(targets.width, targets.height);
    if(!edgeListCapacityResult)
        return false;
    const u32 edgeListCapacityRecords = *edgeListCapacityResult;
    Core::BufferDesc edgeListDesc;
    edgeListDesc
        .setByteSize(static_cast<u64>(sizeof(u32)) * static_cast<u64>(NWB_SW_SHADOW_EDGE_RECORD_WORDS) * static_cast<u64>(edgeListCapacityRecords))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_SoftwareShadowEdgeList))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto edgeListBufferResult = m_graphics.createBuffer(edgeListDesc);
    if(!edgeListBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-list buffer"));
        m_rayTracingState.m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    Core::BufferHandle edgeListBuffer = Move(*edgeListBufferResult);
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !RayTracingDetail::ReplaceHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *edgeListBuffer.get(), m_rayTracingState.m_swShadowEdgeListHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW shadow edge-list buffer in the descriptor heap"));
        m_rayTracingState.m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    m_rayTracingState.m_swShadowEdgeListBuffer = Move(edgeListBuffer);
    m_rayTracingState.m_swShadowEdgeListCapacity = edgeListCapacityRecords;

    LogCreatedShadowReceiverGrid(targets.shadowReceiverFactor, targets.width, targets.height, softHalfWidth, softHalfHeight);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftShadowResolveDispatch SoftShadowResolveDispatchBuilder::Opaque(const OpaqueSoftShadowResolveOptions& options, const RendererRayTracingState& state, DeferredFrameTargets& targets)noexcept{
    Core::Texture* const opaqueResolveMoments = options.frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get();
    const u32 opaqueResolveMomentsSlot = options.frontIsA ? targets.bindless.shadowMomentsB.slot() : targets.bindless.shadowMomentsA.slot();
    Core::Texture* const opaqueWaveletInput = options.temporalActive ? (options.frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get()) : targets.shadowSoftHalfA.get();
    const u32 opaqueWaveletInputSlot = options.temporalActive ? (options.frontIsA ? targets.bindless.shadowHistB.slot() : targets.bindless.shadowHistA.slot()) : targets.bindless.shadowSoftHalfA.slot();
    SoftShadowResolveDispatch opaqueDispatch;
    opaqueDispatch.waveletPipeline = state.m_softShadowResolve.m_scalar.m_wavelet.m_pipeline.get();
    opaqueDispatch.upsamplePipeline = state.m_softShadowResolve.m_scalar.m_upsample.m_pipeline.get();
    opaqueDispatch.firstWaveletResources = {
        opaqueWaveletInput, opaqueResolveMoments, targets.shadowSoftHalfB.get(),
        opaqueWaveletInputSlot, opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    opaqueDispatch.outputHalfAResources = {
        targets.shadowSoftHalfB.get(), opaqueResolveMoments, targets.shadowSoftHalfA.get(),
        targets.bindless.shadowSoftHalfB.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
    };
    opaqueDispatch.outputHalfBResources = {
        targets.shadowSoftHalfA.get(), opaqueResolveMoments, targets.shadowSoftHalfB.get(),
        targets.bindless.shadowSoftHalfA.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    opaqueDispatch.upsampleResources = {
        targets.shadowSoftHalfB.get(), opaqueResolveMoments, targets.shadowSoftHalfB.get(),
        targets.bindless.shadowSoftHalfB.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    opaqueDispatch.visibilityTexture = targets.shadowVisibility.get();
    opaqueDispatch.visibilityStorage = targets.bindless.shadowVisibilityStorage.slot();
    opaqueDispatch.sceneShading = targets.bindless.sceneShading.slot();
    opaqueDispatch.temporalMomentsValid = options.temporalActive;
    opaqueDispatch.graphOwnsWaveletGeometryEntryState = options.graphOwnsGeometryEntry;
    opaqueDispatch.graphOwnsUpsampleStaticEntryStates = true;
    const b8 graphOwnsDeferredOpaqueWaveletInputs = options.phase == SoftShadowOpaqueResolvePhase::WaveletOnly;
    opaqueDispatch.graphOwnsWaveletMomentsEntryState = graphOwnsDeferredOpaqueWaveletInputs;
    opaqueDispatch.graphOwnsFirstWaveletInputState = graphOwnsDeferredOpaqueWaveletInputs || (options.graphOwnsTraceToFirstWavelet && !options.temporalActive);
    opaqueDispatch.graphOwnsFirstWaveletOutputState = options.dispatchResolve && !options.dispatchResolveTail;
    const b8 graphOwnsOneWaveletOpaqueResolveTailEntryStates = !options.dispatchResolve && options.dispatchResolveTail && NWB_SHADOW_RESOLVE_PASS_COUNT == 1u;
    opaqueDispatch.graphOwnsUpsampleInputColorEntryState = graphOwnsOneWaveletOpaqueResolveTailEntryStates;
    opaqueDispatch.graphOwnsUpsampleVisibilityOutputState = graphOwnsOneWaveletOpaqueResolveTailEntryStates;
    opaqueDispatch.firstWaveletWritesHalfA = false;
    opaqueDispatch.fold = SoftShadowUpsampleFold::Overwrite;
    opaqueDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_PASS_COUNT);
    return opaqueDispatch;
}


SoftShadowResolveDispatch SoftShadowResolveDispatchBuilder::Transparent(const TransparentSoftShadowResolveOptions& options, const RendererRayTracingState& state, DeferredFrameTargets& targets)noexcept{
    Core::Texture* const transparentResolveMoments = options.frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get();
    const u32 transparentResolveMomentsSlot = options.frontIsA ? targets.bindless.transparentMomentsB.slot() : targets.bindless.transparentMomentsA.slot();
    Core::Texture* const transparentWaveletInput = options.temporalActive ? (options.frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get()) : targets.transparentSoftHalf.get();
    const u32 transparentWaveletInputSlot = options.temporalActive ? (options.frontIsA ? targets.bindless.transparentHistB.slot() : targets.bindless.transparentHistA.slot()) : targets.bindless.transparentSoftHalf.slot();
    SoftShadowResolveDispatch transparentDispatch;
    transparentDispatch.waveletPipeline = state.m_softShadowResolve.m_rgb.m_wavelet.m_pipeline.get();
    transparentDispatch.upsamplePipeline = state.m_softShadowResolve.m_rgb.m_upsample.m_pipeline.get();
    transparentDispatch.firstWaveletResources = {
        transparentWaveletInput, transparentResolveMoments, targets.shadowSoftHalfA.get(),
        transparentWaveletInputSlot, transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
    };
    transparentDispatch.outputHalfAResources = {
        targets.shadowSoftHalfB.get(), transparentResolveMoments, targets.shadowSoftHalfA.get(),
        targets.bindless.shadowSoftHalfB.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
    };
    transparentDispatch.outputHalfBResources = {
        targets.shadowSoftHalfA.get(), transparentResolveMoments, targets.shadowSoftHalfB.get(),
        targets.bindless.shadowSoftHalfA.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    transparentDispatch.upsampleResources = {
        targets.shadowSoftHalfA.get(), transparentResolveMoments, targets.shadowSoftHalfA.get(),
        targets.bindless.shadowSoftHalfA.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
    };
    transparentDispatch.visibilityTexture = targets.shadowVisibility.get();
    transparentDispatch.visibilityStorage = targets.bindless.shadowVisibilityStorage.slot();
    transparentDispatch.sceneShading = targets.bindless.sceneShading.slot();
    transparentDispatch.temporalMomentsValid = options.temporalActive;
    const b8 graphOwnsTransparentTemporalMergeToWaveletBoundary = options.dispatchResolve && options.splitResolve && !options.dispatchTemporalMerge && options.temporalActive;
    transparentDispatch.graphOwnsFirstWaveletInputState = (options.graphOwnsTraceToResolve && !options.temporalActive) || graphOwnsTransparentTemporalMergeToWaveletBoundary;
    transparentDispatch.graphOwnsWaveletMomentsEntryState = graphOwnsTransparentTemporalMergeToWaveletBoundary;
    transparentDispatch.graphOwnsWaveletGeometryEntryState = options.graphOwnsTraceToResolve;
    transparentDispatch.graphOwnsUpsampleStaticEntryStates = true;
    transparentDispatch.graphOwnsFirstWaveletOutputState = options.dispatchResolve && options.splitResolve;
    const b8 graphOwnsOneWaveletTransparentResolveTailEntryStates = !options.dispatchResolve && options.dispatchResolveTail && NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u;
    transparentDispatch.graphOwnsUpsampleInputColorEntryState = graphOwnsOneWaveletTransparentResolveTailEntryStates;
    transparentDispatch.graphOwnsUpsampleVisibilityOutputState = graphOwnsOneWaveletTransparentResolveTailEntryStates;
    transparentDispatch.firstWaveletWritesHalfA = true;
    transparentDispatch.fold = SoftShadowUpsampleFold::Multiply;
    transparentDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT);
    return transparentDispatch;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SoftShadowSplitResourceUseBuilder::build(const SoftShadowSplitResourceInputs& inputs, SoftShadowSplitResourceUses& uses){
    using namespace RendererTaskGraphDetail;

    const SoftShadowSplitRoute& route = inputs.route;
    const b8 hardwareTransparentTrace = inputs.rayTracingPlan.hardwareTransparentTrace;
    const b8 softShadowHistoryReadable = inputs.rayTracingPlan.softShadowHistoryReadable;
    const b8 opaqueHistoryFrontIsA = inputs.rayTracingPlan.historyFrontIsA;

    const auto shadowSoftHalfAResult = ImportExternalTexture(m_graph, inputs.targets.shadowSoftHalfA, Name(s_ShadowSoftHalfAName), s_ShadowSoftHalfALabel);
    const auto shadowSoftHalfBResult = ImportExternalTexture(m_graph, inputs.targets.shadowSoftHalfB, Name(s_ShadowSoftHalfBName), s_ShadowSoftHalfBLabel);
    const auto shadowSoftGeometryResult = ImportExternalTexture(m_graph, inputs.targets.shadowSoftGeometry, Name(s_ShadowSoftGeometryName), s_ShadowSoftGeometryLabel);
    const auto shadowSoftGeometryPreviousResult = ImportExternalTexture(m_graph, inputs.targets.shadowSoftGeometryPrev, Name(s_ShadowSoftGeometryPreviousName), s_ShadowSoftGeometryPreviousLabel);
    const auto opaqueHistoryAResult = ImportExternalTexture(m_graph, inputs.targets.shadowHistA, Name(s_ShadowHistoryAName), s_ShadowHistoryALabel);
    const auto opaqueHistoryBResult = ImportExternalTexture(m_graph, inputs.targets.shadowHistB, Name(s_ShadowHistoryBName), s_ShadowHistoryBLabel);
    const auto opaqueMomentsAResult = ImportExternalTexture(m_graph, inputs.targets.shadowMomentsA, Name(s_ShadowMomentsAName), s_ShadowMomentsALabel);
    const auto opaqueMomentsBResult = ImportExternalTexture(m_graph, inputs.targets.shadowMomentsB, Name(s_ShadowMomentsBName), s_ShadowMomentsBLabel);
    const auto transparentSoftHalfResult = ImportExternalTexture(m_graph, inputs.targets.transparentSoftHalf, Name(s_TransparentSoftHalfName), s_TransparentSoftHalfLabel);
    const auto transparentHistoryAResult = ImportExternalTexture(m_graph, inputs.targets.transparentHistA, Name(s_TransparentHistoryAName), s_TransparentHistoryALabel);
    const auto transparentHistoryBResult = ImportExternalTexture(m_graph, inputs.targets.transparentHistB, Name(s_TransparentHistoryBName), s_TransparentHistoryBLabel);
    const auto transparentMomentsAResult = ImportExternalTexture(m_graph, inputs.targets.transparentMomentsA, Name(s_TransparentMomentsAName), s_TransparentMomentsALabel);
    const auto transparentMomentsBResult = ImportExternalTexture(m_graph, inputs.targets.transparentMomentsB, Name(s_TransparentMomentsBName), s_TransparentMomentsBLabel);
    const auto importSoftwareBuffer = [&](const Core::BufferHandle& buffer, const Name identity, const AStringView label) -> Expected<Core::GpuGraphResourceId>{
        if(hardwareTransparentTrace)
            return Core::GpuGraphResourceId{};
        return ImportExternalBuffer(m_graph, buffer, identity, label);
    };
    const auto sceneBvhNodesResult = importSoftwareBuffer(inputs.rayTracingResources.sceneBvhNodeBuffer, Name(s_SceneBvhNodesName), s_SceneBvhNodesLabel);
    const auto sceneInstancesResult = importSoftwareBuffer(inputs.rayTracingResources.sceneInstanceBuffer, Name(s_SceneInstancesName), s_SceneInstancesLabel);
    const auto shadowInstanceMaterialsResult = ImportExternalBuffer(m_graph, inputs.rayTracingResources.shadowInstanceMaterialBuffer, Name(s_SceneShadowInstanceMaterialName), s_SceneShadowInstanceMaterialLabel);
    const auto shadowTypedMaterialsResult = ImportExternalBuffer(m_graph, inputs.rayTracingResources.shadowMaterialTypedBuffer, Name(s_SceneShadowMaterialTypedName), s_SceneShadowMaterialTypedLabel);
    const auto shadowInstancesResult = ImportExternalBuffer(m_graph, inputs.rayTracingResources.shadowInstanceBuffer, Name(s_SceneShadowInstancesName), s_SceneShadowInstancesLabel);
    if(
        !shadowSoftHalfAResult
        || !shadowSoftHalfBResult
        || !shadowSoftGeometryResult
        || !shadowSoftGeometryPreviousResult
        || !opaqueHistoryAResult
        || !opaqueHistoryBResult
        || !opaqueMomentsAResult
        || !opaqueMomentsBResult
        || !transparentSoftHalfResult
        || !transparentHistoryAResult
        || !transparentHistoryBResult
        || !transparentMomentsAResult
        || !transparentMomentsBResult
        || (hardwareTransparentTrace ? !inputs.sceneTlas.valid() : (!sceneBvhNodesResult || !sceneInstancesResult))
        || !shadowInstanceMaterialsResult
        || !shadowTypedMaterialsResult
        || !shadowInstancesResult
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import prepared soft-transparent shadow-fold resources"));
        return false;
    }
    const Core::GpuGraphResourceId sceneInstances = *sceneInstancesResult;
    const Core::GpuGraphResourceId sceneBvhNodes = *sceneBvhNodesResult;
    const Core::GpuGraphResourceId shadowSoftHalfA = *shadowSoftHalfAResult;
    const Core::GpuGraphResourceId shadowSoftHalfB = *shadowSoftHalfBResult;
    const Core::GpuGraphResourceId shadowSoftGeometry = *shadowSoftGeometryResult;
    const Core::GpuGraphResourceId shadowSoftGeometryPrevious = *shadowSoftGeometryPreviousResult;
    const Core::GpuGraphResourceId opaqueHistoryA = *opaqueHistoryAResult;
    const Core::GpuGraphResourceId opaqueHistoryB = *opaqueHistoryBResult;
    const Core::GpuGraphResourceId opaqueMomentsA = *opaqueMomentsAResult;
    const Core::GpuGraphResourceId opaqueMomentsB = *opaqueMomentsBResult;
    const Core::GpuGraphResourceId transparentSoftHalf = *transparentSoftHalfResult;
    const Core::GpuGraphResourceId transparentHistoryA = *transparentHistoryAResult;
    const Core::GpuGraphResourceId transparentHistoryB = *transparentHistoryBResult;
    const Core::GpuGraphResourceId transparentMomentsA = *transparentMomentsAResult;
    const Core::GpuGraphResourceId transparentMomentsB = *transparentMomentsBResult;
    const Core::GpuGraphResourceId shadowInstanceMaterials = *shadowInstanceMaterialsResult;
    const Core::GpuGraphResourceId shadowTypedMaterials = *shadowTypedMaterialsResult;
    const Core::GpuGraphResourceId shadowInstances = *shadowInstancesResult;

    // The opaque callback has no temporal/coarse access. Keep its writes apart so a fresh target never reads as synthetic-first.
    uses.opaque.reserve(s_SoftShadowOpaqueResourceUseCapacity + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResources.size()));
    uses.opaque.push_back(ReadUse(inputs.worldPosition));
    uses.opaque.push_back(ReadUse(inputs.normal));
    uses.opaque.push_back(ReadUse(inputs.depth, Core::ResourceStates::ShaderResource));
    uses.opaque.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    uses.opaque.push_back(WriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess));
    uses.opaque.push_back(ReadUse(inputs.sceneGeometryDomain));
    // The map/fallback pair reads only values it has written inside this task; fresh graph entry stays write-only.
    uses.opaque.push_back(WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess));
    uses.opaque.push_back(WriteUse(shadowSoftGeometry, Core::ResourceStates::UnorderedAccess));
    uses.opaque.push_back(ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer));
    uses.opaque.push_back(ReadUse(inputs.lights, Core::ResourceStates::ShaderResource));
    if(route.hardwareShadowSupported){
        if(inputs.sceneTlas.valid())
            uses.opaque.push_back(ReadUse(inputs.sceneTlas, Core::ResourceStates::AccelStructRead));
        if(route.hardwareCsgShadow){
            uses.opaque.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
            uses.opaque.push_back(ReadUse(shadowTypedMaterials, Core::ResourceStates::ShaderResource));
            uses.opaque.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
            uses.opaque.push_back(ReadUse(inputs.materialContextSlots, Core::ResourceStates::ConstantBuffer));
            if(!route.traceGeometryStatesGraphOwned){
                for(const Core::GpuGraphResourceId resource : inputs.traceGeometryResources)
                    uses.opaque.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
            }
        }
    }else{
        uses.opaque.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(shadowTypedMaterials, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        if(inputs.materialContextSlots.valid())
            uses.opaque.push_back(ReadUse(inputs.materialContextSlots, Core::ResourceStates::ConstantBuffer));
        if(!route.traceGeometryStatesGraphOwned){
            for(const Core::GpuGraphResourceId resource : inputs.traceGeometryResources)
                uses.opaque.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }

    uses.opaqueFirstWaveletCount = 0u;
    // The compiler lowers the trace from UAV to the sampled state merge/wavelet needs. First wavelet publishes half-B.
    uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource);
    if(!route.combinedSoftWavelet)
        uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = WriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess);
    uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
    if(route.graphOwnsOpaqueTemporalMergeEntryStates){
        // The frozen selector permits exact selected input/output declarations for the opaque temporal merge.
        const Core::GpuGraphResourceId opaqueHistoryIn = opaqueHistoryFrontIsA ? opaqueHistoryA : opaqueHistoryB;
        const Core::GpuGraphResourceId opaqueMomentsIn = opaqueHistoryFrontIsA ? opaqueMomentsA : opaqueMomentsB;
        const Core::GpuGraphResourceId opaqueHistoryOut = opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA;
        const Core::GpuGraphResourceId opaqueMomentsOut = opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA;
        uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource);
        if(softShadowHistoryReadable){
            uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource);
            uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(opaqueHistoryIn, Core::ResourceStates::ShaderResource);
            uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = ReadUse(opaqueMomentsIn, Core::ResourceStates::ShaderResource);
        }
        uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = WriteUse(opaqueHistoryOut, Core::ResourceStates::UnorderedAccess);
        uses.opaqueFirstWavelet[uses.opaqueFirstWaveletCount++] = WriteUse(opaqueMomentsOut, Core::ResourceStates::UnorderedAccess);
    }

    uses.opaqueResolveCount = 0u;
    if(!route.combinedSoftUpsample){
        // One-wavelet resolve samples only first-wavelet half-B. Keep a conservative ping-pong declaration if passes grow.
        if(NWB_SHADOW_RESOLVE_PASS_COUNT == 1u)
            uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(shadowSoftHalfB, Core::ResourceStates::ShaderResource);
        else{
            uses.opaqueResolve[uses.opaqueResolveCount++] = ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess);
            uses.opaqueResolve[uses.opaqueResolveCount++] = ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess);
        }
        uses.opaqueResolve[uses.opaqueResolveCount++] = WriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(inputs.normal, Core::ResourceStates::ShaderResource);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(inputs.depth, Core::ResourceStates::ShaderResource);
        uses.opaqueResolve[uses.opaqueResolveCount++] = ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer);
    }

    uses.transparentTrace.reserve(s_SoftShadowTransparentTraceResourceUseCapacity + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResources.size()));
    uses.transparentTrace.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource));
    uses.transparentTrace.push_back(ReadUse(inputs.normal, Core::ResourceStates::ShaderResource));
    uses.transparentTrace.push_back(ReadUse(inputs.depth, Core::ResourceStates::ShaderResource));
    uses.transparentTrace.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    uses.transparentTrace.push_back(ReadUse(inputs.sceneGeometryDomain));
    uses.transparentTrace.push_back(WriteUse(transparentSoftHalf, Core::ResourceStates::UnorderedAccess));
    if(hardwareTransparentTrace){
        for(const Core::GpuTaskResourceUse& use : inputs.hardwareSceneReads.uses)
            uses.transparentTrace.push_back(use);
        for(const Core::GpuGraphResourceId scratch : inputs.hardwareTransparentScratch)
            uses.transparentTrace.push_back(ReadWriteUse(scratch, Core::ResourceStates::UnorderedAccess));
    }
    else{
        uses.transparentTrace.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(shadowTypedMaterials, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(inputs.materialContextSlots, Core::ResourceStates::ConstantBuffer));
    }
    uses.transparentTrace.push_back(ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer));
    uses.transparentTrace.push_back(ReadUse(inputs.lights, Core::ResourceStates::ShaderResource));
    if(!route.traceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : inputs.traceGeometryResources)
            uses.transparentTrace.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }

    uses.graphOwnsTransparentTemporalMergeEntryStates = inputs.rayTracingPlan.transparentTemporalMergeReady;
    const b8 transparentHistoryFrontIsA = inputs.rayTracingPlan.historyFrontIsA;
    const Core::GpuGraphResourceId transparentHistoryIn = transparentHistoryFrontIsA ? transparentHistoryA : transparentHistoryB;
    const Core::GpuGraphResourceId transparentMomentsIn = transparentHistoryFrontIsA ? transparentMomentsA : transparentMomentsB;
    const Core::GpuGraphResourceId transparentHistoryOut = transparentHistoryFrontIsA ? transparentHistoryB : transparentHistoryA;
    const Core::GpuGraphResourceId transparentMomentsOut = transparentHistoryFrontIsA ? transparentMomentsB : transparentMomentsA;
    if(!inputs.rayTracingPlan.hardwareTransparentTrace && !inputs.rayTracingPlan.lightSpace.ready && uses.graphOwnsTransparentTemporalMergeEntryStates && softShadowHistoryReadable){
        uses.transparentTrace.push_back(ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(transparentMomentsIn, Core::ResourceStates::ShaderResource));
    }
    uses.transparentTemporalMergeCount = 0u;
    uses.transparentFirstWaveletCount = 0u;
    if(uses.graphOwnsTransparentTemporalMergeEntryStates){
        // Selection freezes at compile. The merge samples the front pair and publishes the opposite; wavelets sample graph-owned.
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource);
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource);
        if(softShadowHistoryReadable){
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(shadowSoftGeometryPrevious, Core::ResourceStates::ShaderResource);
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(transparentHistoryIn, Core::ResourceStates::ShaderResource);
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(transparentMomentsIn, Core::ResourceStates::ShaderResource);
        }
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = WriteUse(transparentHistoryOut, Core::ResourceStates::UnorderedAccess);
        uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = WriteUse(transparentMomentsOut, Core::ResourceStates::UnorderedAccess);
        if(route.combinedSoftTemporal){
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource);
            if(softShadowHistoryReadable){
                uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(opaqueHistoryFrontIsA ? opaqueHistoryA : opaqueHistoryB, Core::ResourceStates::ShaderResource);
                uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = ReadUse(opaqueHistoryFrontIsA ? opaqueMomentsA : opaqueMomentsB, Core::ResourceStates::ShaderResource);
            }
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = WriteUse(opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA, Core::ResourceStates::UnorderedAccess);
            uses.transparentTemporalMerge[uses.transparentTemporalMergeCount++] = WriteUse(opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA, Core::ResourceStates::UnorderedAccess);
        }

        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(transparentHistoryOut, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(transparentMomentsOut, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
    }else{
        // Inactive temporal frames keep their existing trace-to-wavelet route and do not acquire stale-history declarations or a no-op merge callback.
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(transparentSoftHalf, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
    }
    if(route.combinedSoftWavelet){
        // Both temporal merges publish their selected output pair before the fused wavelet reads either pair.
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = ReadUse(opaqueHistoryFrontIsA ? opaqueMomentsB : opaqueMomentsA, Core::ResourceStates::ShaderResource);
        uses.transparentFirstWavelet[uses.transparentFirstWaveletCount++] = WriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess);
    }
    uses.transparentFoldCount = 0u;
    if(route.combinedSoftUpsample)
        uses.transparentFold[uses.transparentFoldCount++] = ReadUse(shadowSoftHalfB, Core::ResourceStates::ShaderResource);
    // One RGB wavelet samples only half-A before multiplying visibility. Keep a ping-pong declaration if passes grow.
    if(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u)
        uses.transparentFold[uses.transparentFoldCount++] = ReadUse(shadowSoftHalfA, Core::ResourceStates::ShaderResource);
    else{
        uses.transparentFold[uses.transparentFoldCount++] = ReadWriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess);
        uses.transparentFold[uses.transparentFoldCount++] = ReadWriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess);
    }
    uses.transparentFold[uses.transparentFoldCount++] = route.combinedSoftUpsample ? WriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess) : ReadWriteUse(inputs.shadowVisibility, Core::ResourceStates::UnorderedAccess);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(shadowSoftGeometry, Core::ResourceStates::ShaderResource);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(inputs.normal, Core::ResourceStates::ShaderResource);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(inputs.depth, Core::ResourceStates::ShaderResource);
    uses.transparentFold[uses.transparentFoldCount++] = ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SoftShadowSplitTaskChainBuilder::declare(const SoftShadowSplitTaskChainInputs& inputs, Core::Alloc::ScratchArena& scratchArena, SoftShadowSplitResourceUses& uses, SoftShadowSplitTasks& tasks){
    using namespace RendererTaskGraphDetail;

    Core::GpuTaskId shadowTraceDependency = inputs.prefixTask;
    Core::GpuGraphResourceId csgShadowContext;
    if(inputs.rayTracingPlan.lightSpace.ready){
        const auto maps = declareLightSpaceShadowMaps(inputs, scratchArena, uses);
        if(!maps)
            return false;
        shadowTraceDependency = maps->shadowTraceDependency;
        csgShadowContext = maps->csgShadowContext;
    }

    Core::GpuTaskSchedulingHint tailScheduling;
    tailScheduling.cost = Core::GpuTaskCostHint::Medium;
    tailScheduling.forceSubmissionBoundary = false;
    tailScheduling.allowPacketMerge = true;
    tailScheduling.mergeWithPrevious = true;
    tailScheduling.allowMergeAcrossConsumerFrontier = inputs.route.csgShadow;
    EnableSameFamilyComputeEffectRouting(tailScheduling);
    EnableCrossFamilyComputeEffectRouting(tailScheduling);
    if(!declareOpaqueTasks(inputs, uses, shadowTraceDependency, tailScheduling, tasks))
        return false;
    return declareTransparentTasks(inputs, uses, tailScheduling, csgShadowContext, scratchArena, tasks);
}

Expected<SoftShadowSplitTaskChainBuilder::LightSpaceShadowMaps> SoftShadowSplitTaskChainBuilder::declareLightSpaceShadowMaps(
    const SoftShadowSplitTaskChainInputs& inputs,
    Core::Alloc::ScratchArena& scratchArena,
    SoftShadowSplitResourceUses& uses
){
    using namespace RendererTaskGraphDetail;

    const SoftShadowSplitRoute& route = inputs.route;
    if((inputs.rayTracingPlan.lightSpace.push.csgFlags & NWB_CSG_SHADOW_FLAG_CURRENT_RECEIVER_CAPS) != 0u){
        const Core::GpuTaskGraph::DeclarationReadView declarations(m_graph);
        const Core::GpuGraphResourceId receiverView = declarations.findImportedBuffer(inputs.rayTracingPlan.lightSpace.receiverView);
        const Core::GpuGraphResourceId receiverCapState = declarations.findImportedBuffer(inputs.rayTracingPlan.lightSpace.receiverCsgSampleState);
        const Core::GpuGraphResourceId receiverCapDepth = declarations.findImportedTexture(inputs.targets.csgRemovedIntervalDepth);
        const Core::GpuGraphResourceId receiverCapData = declarations.findImportedTexture(inputs.targets.csgRemovedIntervalData);
        const Core::GpuGraphResourceId receiverCapCount = declarations.findImportedTexture(inputs.targets.csgRemovedIntervalCount);
        if(!receiverView.valid() || !receiverCapState.valid() || !receiverCapDepth.valid() || !receiverCapData.valid() || !receiverCapCount.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not find the current light-space shadow receiver cap resources"));
            return MakeUnexpected(Failure{});
        }
        const Core::TextureSubresourceSet layers(0u, 1u, 0u, inputs.targets.csgRemovedIntervalLayerCount);
        const Array<Core::GpuTaskResourceUse, 5u> receiverReads{
            ReadUse(receiverView, Core::ResourceStates::ConstantBuffer),
            ReadUse(receiverCapState, Core::ResourceStates::ConstantBuffer),
            ReadTextureUse(receiverCapDepth, layers, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(receiverCapData, layers, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(receiverCapCount, Core::TextureSubresourceSet(0u, 1u, 0u, 1u), Core::ResourceStates::UnorderedAccess),
        };
        for(const auto& use : receiverReads){
            uses.opaque.push_back(use);
            uses.transparentTrace.push_back(use);
        }
    }
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> lightSpaceReads{ scratchArena };
    lightSpaceReads.reserve(s_SoftShadowLightSpaceReadCapacity + inputs.rayTracingPlan.lightSpace.csgDynamicBounds.size() + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResources.size()));
    if(!RendererFramePipelineDetail::AppendRayTracingSceneShadowBuffers(inputs.rayTracingResources, m_graph, lightSpaceReads))
        return MakeUnexpected(Failure{});
    lightSpaceReads.push_back(ReadUse(inputs.materialContextSlots, Core::ResourceStates::ConstantBuffer));
    lightSpaceReads.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    lightSpaceReads.push_back(ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer));
    lightSpaceReads.push_back(ReadUse(inputs.lights, Core::ResourceStates::ShaderResource));
    if(!route.traceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : inputs.traceGeometryResources)
            lightSpaceReads.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> csgBoundsResources{ scratchArena };
    csgBoundsResources.reserve(inputs.rayTracingPlan.lightSpace.csgDynamicBounds.size());
    for(const Core::BufferHandle& buffer : inputs.rayTracingPlan.lightSpace.csgDynamicBounds){
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(m_graph);
            resource = declarations.findImportedBuffer(buffer);
        }
        if(!resource.valid()){
            const auto importedResource = ImportExternalBuffer(m_graph, buffer, buffer->getCreationDescription().debugName, "CSG Shadow Dynamic Bounds");
            if(!importedResource){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import CSG shadow dynamic bounds"));
                return MakeUnexpected(Failure{});
            }
            resource = *importedResource;
        }
        if(!resource.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import CSG shadow dynamic bounds"));
            return MakeUnexpected(Failure{});
        }
        if(FindIf(csgBoundsResources.begin(), csgBoundsResources.end(), [&](const auto existing)noexcept{ return existing == resource; }) != csgBoundsResources.end())
            continue;
        csgBoundsResources.push_back(resource);
        lightSpaceReads.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    auto mapsResult = DeclareLightSpaceShadowMaps(
        LightSpaceShadowGraphInputs{
            .graphics = m_graphics,
            .arena = m_arena,
            .scratchArena = scratchArena,
            .shadowPrepared = inputs.shadowPrepared,
            .snapshot = inputs.rayTracingPlan.lightSpace,
            .dependency = inputs.prefixTask,
            .sceneReads = lightSpaceReads,
            .sceneReadSets = inputs.traceResourceSetUses,
            .stateSources = inputs.stateSources,
        },
        m_graph
    );
    if(!mapsResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare light-space shadow maps"));
        return MakeUnexpected(Failure{});
    }
    const LightSpaceShadowGraph maps = Move(*mapsResult);
    const Array<Core::GpuGraphResourceId, 3u> mapBuffers = { maps.counts, maps.events, maps.views };
    for(const auto resource : mapBuffers){
        uses.opaque.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    LightSpaceShadowMaps result;
    if(maps.csgContext.valid()){
        result.csgShadowContext = maps.csgContext;
        uses.opaque.push_back(ReadWriteUse(maps.csgContext, Core::ResourceStates::UnorderedAccess));
        uses.transparentTrace.push_back(ReadWriteUse(maps.csgContext, Core::ResourceStates::UnorderedAccess));
        uses.opaque.push_back(ReadUse(maps.csgOpaqueDepth, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(maps.csgOpaqueDepth, Core::ResourceStates::ShaderResource));
    }
    const Core::TextureSubresourceSet layers{ 0u, 1u, 0u, inputs.rayTracingPlan.lightSpace.plan.viewCount };
    uses.opaque.push_back(ReadTextureUse(maps.depth, layers, Core::ResourceStates::ShaderResource));
    uses.transparentTrace.push_back(ReadTextureUse(maps.depth, layers, Core::ResourceStates::ShaderResource));
    result.shadowTraceDependency = maps.ready;
    return result;
}

b8 SoftShadowSplitTaskChainBuilder::declareOpaqueTasks(
    const SoftShadowSplitTaskChainInputs& inputs,
    const SoftShadowSplitResourceUses& uses,
    const Core::GpuTaskId& shadowTraceDependency,
    const Core::GpuTaskSchedulingHint& tailScheduling,
    SoftShadowSplitTasks& tasks
){
    using namespace RendererTaskGraphDetail;
    const SoftShadowSplitRoute& route = inputs.route;

    Core::GpuTaskSchedulingHint opaqueScheduling;
    opaqueScheduling.cost = Core::GpuTaskCostHint::Large;
    opaqueScheduling.forceSubmissionBoundary = false;
    opaqueScheduling.allowPacketMerge = true;
    opaqueScheduling.mergeWithPrevious = false;
    EnableSameFamilyComputeEffectRouting(opaqueScheduling, false);
    EnableCrossFamilyComputeEffectRouting(opaqueScheduling);
    Core::GpuTaskDesc opaqueDesc;
    opaqueDesc
        .setIdentity(Name(s_ShadowPassOpaque))
        .setMarkerLabel("Shadow Visibility Opaque")
        .setScheduling(opaqueScheduling)
        .setDependencies({ &shadowTraceDependency, 1u })
        .setExternalDependencies(inputs.historyWriterDrainDependencies)
        .setExternalStateSources(inputs.stateSources)
        .setResourceUses(uses.opaque)
        .setResourceSetUses({
            route.hardwareCsgShadow ? inputs.traceResourceSetUses.data()
                : (!route.hardwareShadowSupported && route.traceGeometryStatesGraphOwned ? &inputs.traceGeometrySetUse : nullptr),
            route.hardwareCsgShadow ? inputs.traceResourceSetUses.size() : (!route.hardwareShadowSupported && route.traceGeometryStatesGraphOwned ? 1u : 0u)
        })
    ;
    const auto opaqueTaskResult = m_raytracingSystem.declareShadowVisibilityOpaqueTask(
        m_graph,
        opaqueDesc,
        inputs.deferredLightingResources,
        inputs.shadowPrepared,
        route.hardwareShadowSupported,
        route.graphOwnsOpaqueTemporalMergeEntryStates,
        &inputs.rayTracingPlan.lightSpace,
        inputs.targets,
        inputs.timingTicket,
        inputs.asyncTiming,
        inputs.shadowVisibilityTiming,
        inputs.opaqueProduced,
        inputs.opaqueFrameIndex
    );
    if(!opaqueTaskResult){
        tasks.opaque = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque shadow-visibility graph task"));
        return false;
    }
    tasks.opaque = *opaqueTaskResult;

    if(!route.combinedSoftTemporal){
        const Array<Core::GpuTaskId, 1u> opaqueFirstWaveletDependencies = { tasks.opaque };
        Core::GpuTaskDesc opaqueFirstWaveletDesc;
        opaqueFirstWaveletDesc
            .setIdentity(route.combinedSoftWavelet ? Name(s_ShadowPassOpaqueTemporalMerge) : Name(s_ShadowPassOpaqueFirstWavelet))
            .setMarkerLabel(route.combinedSoftWavelet ? "Shadow Opaque Temporal Merge" : "Shadow Opaque First Wavelet")
            .setScheduling(tailScheduling)
            .setDependencies(opaqueFirstWaveletDependencies)
            .setExternalStateSources(inputs.stateSources)
            .setResourceUses({ uses.opaqueFirstWavelet.data(), uses.opaqueFirstWaveletCount })
        ;
        const auto opaqueFirstWaveletTaskResult = m_raytracingSystem.declareShadowVisibilityOpaqueFirstWaveletTask(
            m_graph,
            opaqueFirstWaveletDesc,
            inputs.deferredLightingResources,
            inputs.opaqueFrameIndex,
            route.hardwareShadowSupported,
            route.graphOwnsOpaqueTemporalMergeEntryStates,
            route.combinedSoftUpsample,
            route.combinedSoftWavelet,
            inputs.targets,
            inputs.timingTicket,
            inputs.asyncTiming,
            inputs.shadowVisibilityTiming,
            inputs.opaqueResolveTiming,
            inputs.opaqueProduced
        );
        if(!opaqueFirstWaveletTaskResult){
            tasks.opaqueFirstWavelet = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque soft-shadow first-wavelet graph task"));
            return false;
        }
        tasks.opaqueFirstWavelet = *opaqueFirstWaveletTaskResult;
    }

    if(!route.combinedSoftUpsample){
        const Array<Core::GpuTaskId, 1u> opaqueResolveDependencies = { tasks.opaqueFirstWavelet };
        Core::GpuTaskDesc opaqueResolveDesc;
        opaqueResolveDesc
            .setIdentity(Name(s_ShadowPassOpaqueSoftResolve))
            .setMarkerLabel("Shadow Opaque Soft Resolve")
            .setScheduling(tailScheduling)
            .setDependencies(opaqueResolveDependencies)
            .setResourceUses({ uses.opaqueResolve.data(), uses.opaqueResolveCount })
        ;
        const auto opaqueResolveTaskResult = m_raytracingSystem.declareShadowVisibilityOpaqueResolveTailTask(
            m_graph,
            opaqueResolveDesc,
            inputs.deferredLightingResources,
            inputs.opaqueFrameIndex,
            route.hardwareShadowSupported,
            inputs.targets,
            inputs.timingTicket,
            inputs.asyncTiming,
            inputs.shadowVisibilityTiming,
            inputs.opaqueResolveTiming,
            inputs.opaqueProduced
        );
        if(!opaqueResolveTaskResult){
            tasks.opaqueResolve = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque soft-shadow resolve-tail graph task"));
            return false;
        }
        tasks.opaqueResolve = *opaqueResolveTaskResult;
    }
    return true;
}

b8 SoftShadowSplitTaskChainBuilder::declareTransparentTasks(
    const SoftShadowSplitTaskChainInputs& inputs,
    const SoftShadowSplitResourceUses& uses,
    const Core::GpuTaskSchedulingHint& tailScheduling,
    const Core::GpuGraphResourceId csgShadowContext,
    Core::Alloc::ScratchArena& scratchArena,
    SoftShadowSplitTasks& tasks
){
    const SoftShadowSplitRoute& route = inputs.route;

    const Core::GpuTaskId opaqueCompletionTask = SelectOpaqueShadowCompletion(route.combinedSoftTemporal, route.combinedSoftUpsample, tasks.opaque, tasks.opaqueFirstWavelet, tasks.opaqueResolve);
    const Array<Core::GpuTaskId, 1u> traceDependencies = { opaqueCompletionTask };
    Core::GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name(s_ShadowPassSoftTransparentTrace))
        .setMarkerLabel("Shadow Transparent Soft Trace")
        .setScheduling(tailScheduling)
        .setDependencies(traceDependencies)
        .setExternalStateSources(inputs.stateSources)
        .setResourceUses(uses.transparentTrace)
        .setResourceSetUses({ !inputs.traceResourceSetUses.empty() ? inputs.traceResourceSetUses.data() : nullptr, inputs.traceResourceSetUses.size() })
    ;
    const auto transparentTraceTaskResult = m_raytracingSystem.declareShadowTransparentSoftTraceTask(
        m_graph,
        traceDesc,
        inputs.deferredLightingResources,
        inputs.opaqueProduced,
        inputs.opaqueFrameIndex,
        &inputs.rayTracingPlan.lightSpace,
        inputs.targets,
        inputs.timingTicket,
        inputs.transparentTraceProduced
    );
    if(!transparentTraceTaskResult){
        tasks.transparentTrace = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow trace graph task"));
        return false;
    }
    tasks.transparentTrace = *transparentTraceTaskResult;

    Core::GpuTaskId transparentFirstWaveletDependency = tasks.transparentTrace;
    if(uses.graphOwnsTransparentTemporalMergeEntryStates){
        const Array<Core::GpuTaskId, 1u> transparentTemporalMergeDependencies = { transparentFirstWaveletDependency, };
        Core::GpuTaskDesc transparentTemporalMergeDesc;
        transparentTemporalMergeDesc
            .setIdentity(route.combinedSoftTemporal ? Name(s_ShadowPassCombinedTemporalMerge) : Name(s_ShadowPassTransparentTemporalMerge))
            .setMarkerLabel(route.combinedSoftTemporal ? "Shadow Combined Temporal Merge" : "Shadow Transparent Temporal Merge")
            .setScheduling(tailScheduling)
            .setDependencies(transparentTemporalMergeDependencies)
            .setExternalStateSources(inputs.stateSources)
            .setResourceUses({ uses.transparentTemporalMerge.data(), uses.transparentTemporalMergeCount })
        ;
        const auto transparentTemporalMergeTaskResult = m_raytracingSystem.declareShadowTransparentSoftTemporalMergeTask(
            m_graph,
            transparentTemporalMergeDesc,
            inputs.deferredLightingResources,
            inputs.opaqueProduced,
            inputs.opaqueFrameIndex,
            true,
            route.combinedSoftTemporal,
            route.hardwareShadowSupported,
            inputs.targets,
            inputs.timingTicket,
            inputs.transparentResolveTiming,
            inputs.transparentTraceProduced
        );
        if(!transparentTemporalMergeTaskResult){
            tasks.transparentTemporalMerge = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow temporal-merge graph task"));
            return false;
        }
        tasks.transparentTemporalMerge = *transparentTemporalMergeTaskResult;
        transparentFirstWaveletDependency = tasks.transparentTemporalMerge;
    }

    const Array<Core::GpuTaskId, 1u> transparentFirstWaveletDependencies = { transparentFirstWaveletDependency };
    Core::GpuTaskDesc transparentFirstWaveletDesc;
    transparentFirstWaveletDesc
        .setIdentity(route.combinedSoftWavelet ? Name(s_ShadowPassCombinedFirstWavelet) : Name(s_ShadowPassTransparentFirstWavelet))
        .setMarkerLabel(route.combinedSoftWavelet ? "Shadow Combined First Wavelet" : "Shadow Transparent First Wavelet")
        .setScheduling(tailScheduling)
        .setDependencies(transparentFirstWaveletDependencies)
        .setResourceUses({ uses.transparentFirstWavelet.data(), uses.transparentFirstWaveletCount })
    ;
    const auto transparentFirstWaveletTaskResult = m_raytracingSystem.declareShadowTransparentSoftFirstWaveletTask(
        m_graph,
        transparentFirstWaveletDesc,
        inputs.deferredLightingResources,
        inputs.opaqueProduced,
        inputs.opaqueFrameIndex,
        true,
        !uses.graphOwnsTransparentTemporalMergeEntryStates,
        route.combinedSoftWavelet,
        inputs.targets,
        inputs.timingTicket,
        inputs.transparentResolveTiming,
        inputs.transparentTraceProduced
    );
    if(!transparentFirstWaveletTaskResult){
        tasks.transparentFirstWavelet = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow first-wavelet graph task"));
        return false;
    }
    tasks.transparentFirstWavelet = *transparentFirstWaveletTaskResult;

    const Array<Core::GpuTaskId, 1u> foldDependencies = { tasks.transparentFirstWavelet };
    Core::GpuTaskDesc foldDesc;
    foldDesc
        .setIdentity(Name(s_ShadowPassSoftTransparentFold))
        .setMarkerLabel("Shadow Transparent Soft Fold")
        .setScheduling(tailScheduling)
        .setDependencies(foldDependencies)
        .setResourceUses({ uses.transparentFold.data(), uses.transparentFoldCount })
    ;
    // The terminal fold owns effect completion, so lighting, recovery and acceptance observe fully folded visibility.
    const auto foldTaskResult = m_raytracingSystem.declareShadowTransparentSoftFoldTask(
        m_graph,
        foldDesc,
        inputs.deferredLightingResources,
        inputs.opaqueProduced,
        inputs.opaqueFrameIndex,
        route.combinedSoftUpsample,
        inputs.targets,
        inputs.timingTicket,
        inputs.asyncTiming,
        inputs.shadowVisibilityTiming,
        inputs.transparentResolveTiming,
        inputs.transparentTraceProduced
    );
    if(!foldTaskResult){
        tasks.fold = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred soft-transparent shadow-fold graph task"));
        return false;
    }
    tasks.fold = *foldTaskResult;
    if(inputs.rayTracingPlan.lightSpace.diagnosticReadback){
        const auto diagnosticsTaskResult = DeclareLightSpaceShadowDiagnostics(
            LightSpaceShadowGraphInputs{
                .graphics = m_graphics,
                .arena = m_arena,
                .scratchArena = scratchArena,
                .shadowPrepared = inputs.shadowPrepared,
                .snapshot = inputs.rayTracingPlan.lightSpace,
                .dependency = tasks.fold,
                .stateSources = inputs.stateSources,
            },
            csgShadowContext,
            m_graph
        );
        if(!diagnosticsTaskResult){
            tasks.diagnostics = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare CSG shadow diagnostics readback"));
            return false;
        }
        tasks.diagnostics = *diagnosticsTaskResult;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftShadowTemporalMergeRecorder::SoftShadowTemporalMergeRecorder(
    const RendererRayTracingState& state, const u32 slotRangeCount, const u32 halfWidth, const u32 halfHeight, const u32 groupsX, const u32 groupsY, const b8 historyReadable,
    Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap
)noexcept
    : m_state(state)
    , m_commandList(commandList)
    , m_targets(targets)
    , m_heap(heap)
    , m_slotRangeCount(slotRangeCount)
    , m_halfWidth(halfWidth)
    , m_halfHeight(halfHeight)
    , m_groupsX(groupsX)
    , m_groupsY(groupsY)
    , m_historyReadable(historyReadable)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftShadowTemporalMergeRecorder::record(
    const ShadowReprojectMergeHeapResources& resources, const b8 graphOwnsSoftTraceInputState, const b8 graphOwnsMergeCurrentGeometryEntryState, const b8 graphOwnsMergeStaticReadEntryStates,
    const b8 graphOwnsMergeTemporalEntryStates
)const{
    const u32 historyValid = m_historyReadable ? 1u : 0u;
    NWB_ASSERT(resources.softTrace && resources.historyIn && resources.momentsIn && resources.historyOut && resources.momentsOut);
    if(!graphOwnsSoftTraceInputState)
        m_commandList.setTextureState(resources.softTrace, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
    if(!graphOwnsMergeTemporalEntryStates){
        if(m_historyReadable){
            m_commandList.setTextureState(resources.historyIn, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
            m_commandList.setTextureState(resources.momentsIn, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        }
        m_commandList.setTextureState(resources.historyOut, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        m_commandList.setTextureState(resources.momentsOut, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    }
    if(!graphOwnsMergeCurrentGeometryEntryState)
        m_commandList.setTextureState(m_targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    if(!graphOwnsMergeStaticReadEntryStates){
        if(m_historyReadable)
            m_commandList.setTextureState(m_targets.shadowSoftGeometryPrev.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        m_commandList.setTextureState(m_targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    }
    m_commandList.setEnableUavBarriersForTexture(resources.historyOut, true);
    m_commandList.setEnableUavBarriersForTexture(resources.momentsOut, true);
    m_commandList.commitBarriers();

    const ShadowReprojectMergePushConstants push = MakeShadowReprojectMergePushConstants(
        Span<const f32>(m_state.m_prevWorldToClip.raw, 16u),
        m_targets.width,
        m_targets.height,
        m_halfWidth,
        m_halfHeight,
        m_slotRangeCount,
        historyValid,
        resources.softTraceSlot,
        resources.historyInSlot,
        resources.momentsInSlot,
        m_targets.bindless.shadowSoftGeometry.slot(),
        m_targets.bindless.shadowSoftGeometryPrev.slot(),
        m_targets.bindless.gbufferWorldPosition.slot(),
        resources.historyOutStorageSlot,
        resources.momentsOutStorageSlot,
        m_targets.shadowReceiverFactor
    );

    m_commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_state.m_shadowReprojectMergePipeline));
    m_heap.bindCompute(m_commandList, *m_state.m_shadowReprojectMergePipeline.get());
    m_commandList.setPushConstants(&push, sizeof(push));
    m_commandList.dispatch(m_groupsX, m_groupsY, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftwareAdaptiveShadowRecorder::Record(const GraphOwnedAdaptiveShadowPlan* const plan, const RendererRayTracingState& state, Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap){
    const auto makePush = [&]()noexcept{
        return MakeSwShadowHeapPushConstants(
            state.m_sceneBvhInstanceCount,
            targets.bindless.slotsBufferDescriptor.slot(),
            state.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
            targets.bindless.shadowVisibilityStorage.slot(),
            targets.bindless.shadowCoarseTransmittanceStorage.slot(),
            targets.bindless.shadowSoftHalfAStorage.slot(),
            targets.bindless.transparentSoftHalfStorage.slot(),
            state.m_swShadowEdgeCounterHeapHandle.slot(),
            state.m_swShadowEdgeListHeapHandle.slot(),
            state.m_swShadowIndirectArgsHeapHandle.slot(),
            targets.shadowReceiverFactor
        );
    };

    const u32 groupSize = static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 fullGroupsX = DivideUp(targets.width, groupSize);
    const u32 fullGroupsY = DivideUp(targets.height, groupSize);
    const u32 coarseWidth = DivideUpWrapping(targets.width, NWB_SW_SHADOW_COARSE_FACTOR);
    const u32 coarseHeight = DivideUpWrapping(targets.height, NWB_SW_SHADOW_COARSE_FACTOR);
    const u32 coarseGroupsX = DivideUp(coarseWidth, groupSize);
    const u32 coarseGroupsY = DivideUp(coarseHeight, groupSize);

    // Compacted mode traces only classified edge records.
    const b8 graphOwnsAdaptivePlan = plan && plan->enabled;
    const b8 compact = graphOwnsAdaptivePlan && plan->compact;

    // Coarse transmittance feeds both adaptive resolve modes.
    commandList.setEnableUavBarriersForTexture(targets.shadowCoarseTransmittance.get(), true);
    commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();
    SwShadowHeapPushConstants coarsePush = makePush();
    coarsePush.width = targets.width;
    coarsePush.height = targets.height;
    coarsePush.coarseWidth = coarseWidth;
    coarsePush.coarseHeight = coarseHeight;
    commandList.setComputeState(RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentCoarsePipeline));
    heap.bindCompute(commandList, *state.m_swShadowTransparentCoarsePipeline.get());
    commandList.setPushConstants(&coarsePush, sizeof(coarsePush));
    commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);

    // Synchronize the coarse write before resolve.
    commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    if(compact){
        // The append list is bounded by this frame's reset counter.
        commandList.setEnableUavBarriersForBuffer(state.m_swShadowEdgeCounterBuffer.get(), true);
        commandList.setEnableUavBarriersForBuffer(state.m_swShadowEdgeListBuffer.get(), true);
        commandList.setBufferState(state.m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        // Classify interpolated interiors and append traceable edges.
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants classifyPush = makePush();
        classifyPush.width = targets.width;
        classifyPush.height = targets.height;
        classifyPush.coarseWidth = coarseWidth;
        classifyPush.coarseHeight = coarseHeight;
        classifyPush.edgeCapacity = state.m_swShadowEdgeListCapacity;
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentClassifyPipeline));
        heap.bindCompute(commandList, *state.m_swShadowTransparentClassifyPipeline.get());
        commandList.setPushConstants(&classifyPush, sizeof(classifyPush));
        commandList.dispatch(fullGroupsX, fullGroupsY, 1u);

        // Classify produces the list and in-place visibility writes.
        commandList.setBufferState(state.m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        // Build indirect dispatch arguments from the clamped list count.
        commandList.setBufferState(state.m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants argsPush = makePush();
        argsPush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
        argsPush.edgeCapacity = state.m_swShadowEdgeListCapacity;
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentBuildArgsPipeline));
        heap.bindCompute(commandList, *state.m_swShadowTransparentBuildArgsPipeline.get());
        commandList.setPushConstants(&argsPush, sizeof(argsPush));
        commandList.dispatch(1u, 1u, 1u);

        // Indirect dispatch consumes the generated arguments and list.
        commandList.setBufferState(state.m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        SwShadowHeapPushConstants tracePush = makePush();
        tracePush.width = targets.width;
        tracePush.height = targets.height;
        tracePush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::IndirectArgument);
        commandList.commitBarriers();
        Core::ComputeState computeStateIndirect = RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentIndirectPipeline);
        computeStateIndirect.setIndirectParams(state.m_swShadowIndirectArgsBuffer.get());
        commandList.setComputeState(computeStateIndirect);
        heap.bindCompute(commandList, *state.m_swShadowTransparentIndirectPipeline.get());
        commandList.setPushConstants(&tracePush, sizeof(tracePush));
        commandList.dispatchIndirect(0u);
    }
    else{
        // Full-resolution adaptive fallback.
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants resolvePush = makePush();
        resolvePush.width = targets.width;
        resolvePush.height = targets.height;
        resolvePush.coarseWidth = coarseWidth;
        resolvePush.coarseHeight = coarseHeight;
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentResolvePipeline));
        heap.bindCompute(commandList, *state.m_swShadowTransparentResolvePipeline.get());
        commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
        commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftwareTransparentShadowTraceRecorder::Record(const SoftwareTransparentShadowTraceInputs& inputs, const RendererRayTracingState& state, Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap){
    // The software route is retained for devices without hardware ray queries.
    if(!inputs.graphOwnsOpaqueToTransparentBoundary){
        commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    }
    commandList.setEnableUavBarriersForTexture(targets.transparentSoftHalf.get(), true);
    if(!inputs.graphOwnsOpaqueToTransparentBoundary)
        commandList.commitBarriers();

    const b8 samplingHistoryUsable = inputs.temporalHistoryReadable && state.m_softTransparentTemporalReady && state.m_softwareTransparentSampling.m_history.usable();
    if(samplingHistoryUsable && !inputs.graphOwnsOpaqueToTransparentBoundary){
        // These reads also belong to the split trace task; unsplit phases retain local transitions.
        commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.shadowSoftGeometryPrev.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(inputs.historyFrontIsA ? targets.transparentMomentsA.get() : targets.transparentMomentsB.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }
    const SoftwareTransparentSamplingPush tracePush = MakeSoftwareTransparentSamplingPush(
        Span<const f32>(state.m_prevWorldToClip.raw, 16u),
        targets.width,
        targets.height,
        state.m_sceneBvhInstanceCount,
        inputs.frameIndex,
        inputs.sampleCount,
        targets.bindless.slotsBufferDescriptor.slot(),
        state.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        targets.bindless.transparentSoftHalfStorage.slot(),
        samplingHistoryUsable,
        targets.bindless.shadowSoftGeometry.slot(),
        targets.bindless.shadowSoftGeometryPrev.slot(),
        inputs.historyFrontIsA ? targets.bindless.transparentMomentsA.slot() : targets.bindless.transparentMomentsB.slot(),
        targets.shadowReceiverFactor
    );
    const auto& pipeline = state.m_softwareTransparentSampling.m_pipelines[samplingHistoryUsable ? 1u : 0u];
    commandList.setComputeState(RayTracingDetail::MakeComputePassState(pipeline));
    heap.bindCompute(commandList, *pipeline.get());
    commandList.setPushConstants(&tracePush, sizeof(tracePush));
    commandList.dispatch(inputs.groupsX, inputs.groupsY, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPipelineSet::ensureShadowPipeline(){
    if(m_shadow.m_shadowPipeline)
        return true;
    if(m_shadow.m_shadowPipelineFailed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayQuery) || !m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    // Trace layouts are push-only; resources come from the global heap.
    static_assert(sizeof(ShadowRqSoftPushConstants) >= sizeof(ShadowRqPushConstants), "shadow-trace push-constant range must cover both the hard and soft trace push structs");
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(ShadowRqSoftPushConstants), Core::ShaderType::Compute, m_shadow.m_shadowBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    if(
        !m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::shadowRayqueryCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_ShadowRayQuery", m_shadow.m_shadowShader)
        || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_shadow.m_shadowShader, m_shadow.m_shadowBindingLayout, true, m_shadow.m_shadowPipeline)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery shadow compute pipeline"));
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery shadow compute pipeline"));
    return true;
}

b8 ShadowPipelineSet::ensureShadowSoftPipeline(){
    if(m_softShadow.m_shadowSoftPipeline)
        return true;
    if(m_softShadow.m_shadowSoftPipelineFailed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayQuery) || !m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: soft RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    // Soft and hard traces share their push-only layout.
    if(!m_shadow.m_shadowBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow binding layout missing for the soft RayQuery pipeline"));
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    if(
        !m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::shadowRayquerySoftCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_ShadowRayQuerySoft", m_softShadow.m_shadowSoftShader)
        || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_softShadow.m_shadowSoftShader, m_shadow.m_shadowBindingLayout, true, m_softShadow.m_shadowSoftPipeline)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery soft shadow compute pipeline"));
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery soft shadow compute pipeline"));
    return true;
}

b8 ShadowPipelineSet::ensureSwShadowPipeline(){
    if(m_shadow.m_swShadowPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software shadows require the initialized global descriptor heap"));
        m_shadow.m_swShadowPipelineFailed = true;
        return false;
    }

    if(!m_shadow.m_swShadowBindingLayout){
        if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(SwShadowHeapPushConstants), Core::ShaderType::Compute, m_shadow.m_swShadowBindingLayout)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding layout"));
            m_shadow.m_swShadowPipelineFailed = true;
            return false;
        }

        // Compaction uses a persistent counter and UAV-writable indirect-args buffer.
        Core::BufferDesc edgeCounterDesc;
        edgeCounterDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_COUNTER_SIZE))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name(s_SoftwareShadowEdgeCounter))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto swShadowEdgeCounterBufferResult = m_graphics.createBuffer(edgeCounterDesc);
        if(swShadowEdgeCounterBufferResult)
            m_shadow.m_swShadowEdgeCounterBuffer = Move(*swShadowEdgeCounterBufferResult);
        else
            m_shadow.m_swShadowEdgeCounterBuffer.reset();
        if(!swShadowEdgeCounterBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-counter buffer"));
            m_shadow.m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc indirectArgsDesc;
        indirectArgsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_INDIRECT_ARGS_WORD_COUNT))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name(s_SoftwareShadowIndirectArgs))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto swShadowIndirectArgsBufferResult = m_graphics.createBuffer(indirectArgsDesc);
        if(swShadowIndirectArgsBufferResult)
            m_shadow.m_swShadowIndirectArgsBuffer = Move(*swShadowIndirectArgsBufferResult);
        else
            m_shadow.m_swShadowIndirectArgsBuffer.reset();
        if(!swShadowIndirectArgsBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow indirect-args buffer"));
            m_shadow.m_swShadowPipelineFailed = true;
            return false;
        }
    }

    const b8 heapResourcesReady =
        RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_shadow.m_swShadowEdgeCounterBuffer.get(), m_shadow.m_swShadowEdgeCounterHeapHandle)
        && RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_shadow.m_swShadowIndirectArgsBuffer.get(), m_shadow.m_swShadowIndirectArgsHeapHandle)
    ;
    if(!heapResourcesReady){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-shadow work buffers in the descriptor heap"));
        m_shadow.m_swShadowPipelineFailed = true;
        return false;
    }

    const b8 passesReady =
        ensureSwShadowPassPipeline(m_shadow.m_swShadowOpaquePrepassShader, m_shadow.m_swShadowOpaquePrepassPipeline, &ShadowSubsystem::swShadowOpaquePrepassCs, "ECSRender_SwShadowOpaquePrepass")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowSoftOpaqueShader, m_shadow.m_swShadowSoftOpaquePipeline, &ShadowSubsystem::swShadowSoftOpaqueCs, "ECSRender_SwShadowSoftOpaque")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentCoarseShader, m_shadow.m_swShadowTransparentCoarsePipeline, &ShadowSubsystem::swShadowTransparentCoarseCs, "ECSRender_SwShadowTransparentCoarse")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentResolveShader, m_shadow.m_swShadowTransparentResolvePipeline, &ShadowSubsystem::swShadowTransparentResolveCs, "ECSRender_SwShadowTransparentResolve")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentClassifyShader, m_shadow.m_swShadowTransparentClassifyPipeline, &ShadowSubsystem::swShadowTransparentClassifyCs, "ECSRender_SwShadowTransparentClassify")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentBuildArgsShader, m_shadow.m_swShadowTransparentBuildArgsPipeline, &ShadowSubsystem::swShadowTransparentBuildargsCs, "ECSRender_SwShadowTransparentBuildArgs")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentIndirectShader, m_shadow.m_swShadowTransparentIndirectPipeline, &ShadowSubsystem::swShadowTransparentIndirectCs, "ECSRender_SwShadowTransparentIndirect")
        && ensureSoftwareTransparentSamplingPipeline()
    ;
    if(!passesReady){
        m_shadow.m_swShadowPipelineFailed = true;
        return false;
    }
    return true;
}

b8 ShadowPipelineSet::ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::* shaderMember, const NameText<char> debugLabel){
    if(pipeline)
        return true;

    if(!m_shaderSystem.loadShader<ComputeShader>(shaderMember, Core::ShaderArchive::s_DefaultVariant, Name(debugLabel), shader))
        return false;
    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(m_graphics.getDevice(), shader, m_shadow.m_swShadowBindingLayout, false, pipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
        return false;
    }
    return true;
}

b8 RayTraceMaterialResources::ensureShadowInstanceMaterialBuffer(usize instanceCount){
    // CPU-uploaded material context is shared by the exclusive HW/SW backends.
    if(m_shadow.m_shadowInstanceMaterialBuffer && m_shadow.m_shadowInstanceMaterialCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowInstanceMaterialBuffer.get(), m_shadow.m_shadowInstanceMaterialHeapHandle);

    const usize capacity = ::NextGrowingCapacity(m_shadow.m_shadowInstanceMaterialCapacity, instanceCount, s_ShadowInstanceMaterialInitialCapacity);

    Core::BufferDesc materialBufferDesc;
    materialBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbRtInstanceMaterialGpu) * capacity))
        .setStructStride(sizeof(NwbRtInstanceMaterialGpu))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_ShadowResourceInstanceMaterial))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto materialBufferResult = m_graphics.createBuffer(materialBufferDesc);
    if(!materialBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance material buffer"));
        return false;
    }
    Core::BufferHandle materialBuffer = Move(*materialBufferResult);
    if(!replaceRayTraceMaterialContextHeapHandle(*materialBuffer.get(), m_shadow.m_shadowInstanceMaterialHeapHandle))
        return false;
    m_shadow.m_shadowInstanceMaterialBuffer = Move(materialBuffer);
    m_shadow.m_shadowInstanceMaterialCapacity = capacity;
    return true;
}

b8 RayTraceMaterialResources::ensureShadowInstanceContextBuffer(usize instanceCount){
    // Shadow tracing needs an instance record for every gathered occluder.
    if(instanceCount == 0u)
        return !m_shadow.m_shadowInstanceBuffer || ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowInstanceBuffer.get(), m_shadow.m_shadowInstanceHeapHandle);
    if(m_shadow.m_shadowInstanceBuffer && m_shadow.m_shadowInstanceCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowInstanceBuffer.get(), m_shadow.m_shadowInstanceHeapHandle);

    const usize capacity = ::NextGrowingCapacity(m_shadow.m_shadowInstanceCapacity, instanceCount);
    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_ShadowResourceInstanceContext))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto instanceBufferResult = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance context buffer"));
        return false;
    }
    Core::BufferHandle instanceBuffer = Move(*instanceBufferResult);
    if(!replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), m_shadow.m_shadowInstanceHeapHandle))
        return false;
    m_shadow.m_shadowInstanceBuffer = Move(instanceBuffer);
    m_shadow.m_shadowInstanceCapacity = capacity;
    return true;
}

b8 RayTraceMaterialResources::ensureShadowMaterialTypedBuffer(usize byteCount){
    // Keep one word so the heap binding remains valid with no transparent occluders.
    const usize requiredByteCount = ResolveMaterialTypedBufferByteCount(byteCount, sizeof(u32));
    if(m_shadow.m_shadowMaterialTypedBuffer && m_shadow.m_shadowMaterialTypedCapacity >= requiredByteCount)
        return ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowMaterialTypedBuffer.get(), m_shadow.m_shadowMaterialTypedHeapHandle);

    const usize capacity = ::NextGrowingCapacity(m_shadow.m_shadowMaterialTypedCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_ShadowResourceMaterialTyped))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto materialTypedBufferResult = m_graphics.createBuffer(materialTypedBufferDesc);
    if(!materialTypedBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow material typed buffer"));
        return false;
    }
    Core::BufferHandle materialTypedBuffer = Move(*materialTypedBufferResult);
    if(!replaceRayTraceMaterialContextHeapHandle(*materialTypedBuffer.get(), m_shadow.m_shadowMaterialTypedHeapHandle))
        return false;
    m_shadow.m_shadowMaterialTypedBuffer = Move(materialTypedBuffer);
    m_shadow.m_shadowMaterialTypedCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return;

    // White transmittance is the all-lit fallback.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(*targets.shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::Color(1.f, 1.f, 1.f, 1.f));
}

RayTracingShadowVisibilityGraphPlanSnapshot RendererRayTracingSystem::snapshotShadowVisibilityGraphPlan(const b8 hardwareShadowSupported)const noexcept{
    const auto& state = m_rayTracingState;
    const b8 hardwareTransparentTrace = hardwareShadowSupported && state.m_softTransparentReady && hardwareTransparentShadowReady();
    const b8 transparentSceneReady = hardwareTransparentTrace ? state.m_tlas && state.m_tlasHeapHandle.valid() : state.m_sceneBvhNodeBuffer && state.m_sceneInstanceBuffer;
    const b8 softTransparentFoldReady =
        state.m_softShadowReady
        && state.m_softShadowSlotMask != 0u
        && softTransparentShadowReady()
        && transparentSceneReady
        && state.m_shadowInstanceMaterialBuffer
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowInstanceBuffer
    ;
    GraphOwnedAdaptiveShadowPlan adaptivePlan;
    if(!softTransparentShadowReady() && !hardwareShadowSupported && shadowVisibilitySoftwareResourcesPreflighted() && state.m_swShadowEdgeCounterBuffer){
        adaptivePlan.enabled = true;
        adaptivePlan.compact = true;
    }

    return RayTracingShadowVisibilityGraphPlanSnapshot{
        .lightSpace = lightSpaceShadowSnapshot(),
        .adaptivePlan = adaptivePlan,
        .hardwareTransparentTrace = hardwareTransparentTrace,
        .softTransparentFoldReady = softTransparentFoldReady,
        .combinedSoftUpsample = softTransparentFoldReady
            && state.m_softShadowResolve.m_combinedUpsample.m_pipeline
            && NWB_SHADOW_RESOLVE_PASS_COUNT == 1u && NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u,
        .combinedSoftWaveletReady = state.m_softShadowResolve.m_combinedWavelet.m_pipeline
            && state.m_softShadowTemporalReady && state.m_softTransparentTemporalReady,
        .combinedSoftTemporalReady = static_cast<b8>(state.m_softShadowResolve.m_combinedTemporal.m_pipeline),
        .softShadowHistoryReadable = SoftShadowTemporalHistoryUsable(
            state.m_softShadowTemporalReady,
            state.m_prevWorldToClipValid,
            state.m_softShadowTemporalSeeded
        ),
        .opaqueTemporalMergeReady = state.m_softShadowTemporalReady,
        .transparentTemporalMergeReady = state.m_softTransparentTemporalReady,
        .historyFrontIsA = state.m_softShadowHistoryFrontIsA != 0u,
    };
}

Expected<u32> RendererRayTracingSystem::renderGpuBvhShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 splitOpaqueSoftResolve,
    const GraphOwnedAdaptiveShadowPlan* const graphOwnedAdaptivePlan,
    const LightSpaceShadowSnapshot* const lightSpace
){
    u32 producedFrameIndex = 0u;
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    if(!targets.shadowVisibility)
        return MakeUnexpected(Failure{});
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());
    if(!m_rayTracingState.m_sceneBvhNodeBuffer || m_rayTracingState.m_sceneBvhInstanceCount == 0u)
        return MakeUnexpected(Failure{});
    if(!m_rayTracingState.m_swShadowOpaquePrepassPipeline || m_rayTracingState.m_swShadowMeshCount == 0u)
        return MakeUnexpected(Failure{});

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowCoarseTransmittanceStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.transparentSoftHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowEdgeCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowEdgeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software-shadow heap resources are incomplete"));
        return MakeUnexpected(Failure{});
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, m_graphics.getDevice(), commandList);

    // Subsequent visibility passes read/write this UAV in place.
    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);

    const auto makePush = [&]()noexcept{
        return MakeSwShadowHeapPushConstants(
            m_rayTracingState.m_sceneBvhInstanceCount,
            targets.bindless.slotsBufferDescriptor.slot(),
            m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
            targets.bindless.shadowVisibilityStorage.slot(),
            targets.bindless.shadowCoarseTransmittanceStorage.slot(),
            targets.bindless.shadowSoftHalfAStorage.slot(),
            targets.bindless.transparentSoftHalfStorage.slot(),
            m_rayTracingState.m_swShadowEdgeCounterHeapHandle.slot(),
            m_rayTracingState.m_swShadowEdgeListHeapHandle.slot(),
            m_rayTracingState.m_swShadowIndirectArgsHeapHandle.slot(),
            targets.shadowReceiverFactor
        );
    };

    const u32 groupSize = static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 fullGroupsX = DivideUp(targets.width, groupSize);
    const u32 fullGroupsY = DivideUp(targets.height, groupSize);

    // Skip the fallback after a soft transparent fold.
    b8 softTransparentRan = false;

    // Soft upsample overwrites the opaque prepass.
    const b8 softWillRun = m_rayTracingState.m_softShadowReady && m_rayTracingState.m_softShadowSlotMask != 0u;
    if(!softWillRun){
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants opaquePush = makePush();
        opaquePush.width = targets.width;
        opaquePush.height = targets.height;
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowOpaquePrepassPipeline));
        heap.bindCompute(commandList, *m_rayTracingState.m_swShadowOpaquePrepassPipeline.get());
        commandList.setPushConstants(&opaquePush, sizeof(opaquePush));
        commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
    }

    // The transparent pass reads and multiplies the opaque mask in place.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    // Soft opaque resolve replaces the full-resolution mask.
    if(softWillRun){
        const u32 softGroupsX = ResolveWrappingRayTracingDispatchGroup(targets.width, targets.shadowReceiverFactor, groupSize);
        const u32 softGroupsY = ResolveWrappingRayTracingDispatchGroup(targets.height, targets.shadowReceiverFactor, groupSize);

        // Advance the primary producer's jitter sequence once.
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;

        // Resolve reads the soft trace and geometry scratch in place.
        enableSoftShadowResolveBarriers(commandList, targets);

        commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        {
            Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

            SwShadowHeapPushConstants softTracePush = makePush();
            softTracePush.width = targets.width;
            softTracePush.height = targets.height;
            softTracePush.frameIndex = frameIndex;
            softTracePush.softSampleCount = softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP;
            if(lightSpace && lightSpace->ready){
                NWB_ASSERT(splitSoftTransparentFold);
                if(!RecordLightSpaceResolve(*lightSpace, frameIndex, softTracePush.softSampleCount, targets.bindless.shadowSoftHalfAStorage.slot(), false, commandList, heap, m_graphics.gpuTiming(), *targets.shadowSoftHalfA))
                    return MakeUnexpected(Failure{});
                LightSpaceShadowLogReporter{}.reportCsgDispatch(
                    (lightSpace->push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u,
                    false,
                    lightSpace->push.instanceCount,
                    m_lightSpaceShadow.m_csgDispatchLogged
                );
                LightSpaceShadowLogReporter{}.reportShadowMapDispatch(m_lightSpaceShadow.m_dispatchLogged);
            }
            else{
                commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowSoftOpaquePipeline));
                heap.bindCompute(commandList, *m_rayTracingState.m_swShadowSoftOpaquePipeline.get());
                commandList.setPushConstants(&softTracePush, sizeof(softTracePush));
                commandList.dispatch(softGroupsX, softGroupsY, 1u);
            }
        }

        // The split resolver declares this same-UAV dependency, so its graph prologue owns the trace fence.
        // Unsplit soft resolve phases retain their internal UAV fence.
        dispatchPrimarySoftShadowResolve(commandList, targets, deferredLightingResources, frameIndex, softGroupsX, softGroupsY, splitOpaqueSoftResolve, splitSoftTransparentFold, graphOwnsOpaqueTemporalMergeEntryStates);
        if(splitSoftTransparentFold){
            // The transparent tail records later, so preserve the normal route's one-shot traversal diagnostic first.
            reportSoftwareShadowTraversal(targets);
            return producedFrameIndex;
        }
        softTransparentRan = m_rayTracingState.m_softTransparentReady;
    }

    if(!softTransparentRan)
        SoftwareAdaptiveShadowRecorder::Record(graphOwnedAdaptivePlan, m_rayTracingState, commandList, targets, heap);

    reportSoftwareShadowTraversal(targets);
    return producedFrameIndex;
}

Expected<u32> RendererRayTracingSystem::renderGpuBvhShadowVisibilityOpaque(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const LightSpaceShadowSnapshot* const lightSpace
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return MakeUnexpected(Failure{});
    return renderGpuBvhShadowVisibility(commandList, targets, deferredLightingResources, true, graphOwnsOpaqueTemporalMergeEntryStates, true, nullptr, lightSpace);
}

void RendererRayTracingSystem::enableSoftShadowResolveBarriers(Core::CommandList& commandList, DeferredFrameTargets& targets){
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
    // Temporal merge writes history before later reads.
    if(m_rayTracingState.m_softShadowTemporalReady){
        commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
    }
}

void RendererRayTracingSystem::dispatchPrimarySoftShadowResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const u32 softGroupsX,
    const u32 softGroupsY,
    const b8 splitOpaqueSoftResolve,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates
){
    // Split resolve declares the trace fence; the direct route retains its UAV fence.
    if(!splitOpaqueSoftResolve){
        commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
        true,
        !splitOpaqueSoftResolve,
        !splitSoftTransparentFold,
        !splitSoftTransparentFold,
        false,
        false,
        false,
        graphOwnsOpaqueTemporalMergeEntryStates,
        false,
        !splitOpaqueSoftResolve,
        false
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()noexcept{
    if(m_preparedShadowMaterialContext.publishAcceptedUpload(m_rayTracingState))
        clearPreparedShadowMaterialContext();
}

// The frozen software traversal indexes this context, so a new context always drops it.
b8 RendererRayTracingSystem::capturePreparedShadowMaterialContext(
    const PreparedShadowMaterialContextRoute::Enum route,
    const b8 staticScene,
    const u64 hash,
    const Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
    const Span<const InstanceGpuData> instances,
    const Span<const u8> materialTypedBytes
){
    if(!m_preparedShadowMaterialContext.capture(m_rayTracingState, route, staticScene, hash, instanceMaterials, instances, materialTypedBytes))
        return false;
    m_preparedSceneSwBvhTraversal.clear();
    return true;
}

b8 RendererRayTracingSystem::capturePreparedShadowMaterialContextCacheReuse(const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    m_preparedSceneSwBvhTraversal.clear();
    return m_preparedShadowMaterialContext.captureCacheReuse(m_rayTracingState, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
}

void RendererRayTracingSystem::clearPreparedShadowMaterialContext()noexcept{
    m_preparedShadowMaterialContext.clear();
    m_preparedSceneSwBvhTraversal.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowVisibilityOpaqueTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& prepared,
    const b8 hardwareShadowSupported,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const LightSpaceShadowSnapshot* const lightSpace,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    b8& opaqueProduced,
    u32& opaqueFrameIndex
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
            .lightSpace = lightSpace ? *lightSpace : LightSpaceShadowSnapshot{},
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .prepared = prepared,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowVisibilityOpaqueFirstWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32& opaqueFrameIndex,
    const b8 hardwareShadowSupported,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 deferUpsample,
    const b8 deferWavelet,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
    b8& opaqueProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueFirstWaveletGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueFirstWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueResolveTiming = opaqueResolveTiming,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
            .deferUpsample = deferUpsample,
            .deferWavelet = deferWavelet,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowVisibilityOpaqueResolveTailTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32& opaqueFrameIndex,
    const b8 hardwareShadowSupported,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
    b8& opaqueProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueResolveTailGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueResolveTailGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueResolveTiming = opaqueResolveTiming,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .hardwareShadowSupported = hardwareShadowSupported,
        });
}

Expected<u32> RendererRayTracingSystem::renderShadowVisibilityOpaque(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const LightSpaceShadowSnapshot* const lightSpace
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return MakeUnexpected(Failure{});
    return renderShadowVisibility(commandList, targets, deferredLightingResources, true, graphOwnsOpaqueTemporalMergeEntryStates, true, lightSpace);
}

b8 RendererRayTracingSystem::renderSoftOpaqueShadowResolvePhase(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const b8 hardwareShadowSupported,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const SoftShadowOpaqueResolvePhase::Enum phase
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u || (phase != SoftShadowOpaqueResolvePhase::TemporalAndWavelet && !m_rayTracingState.m_softShadowTemporalReady))
        return false;
    const u32 groupSize = hardwareShadowSupported ? static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE) : static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 softGroupsX = ResolveWrappingRayTracingDispatchGroup(targets.width, targets.shadowReceiverFactor, groupSize);
    const u32 softGroupsY = ResolveWrappingRayTracingDispatchGroup(targets.height, targets.shadowReceiverFactor, groupSize);
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
        false,
        true,
        false,
        false,
        true,
        false,
        false,
        graphOwnsOpaqueTemporalMergeEntryStates,
        false,
        false,
        true,
        false,
        false,
        false,
        phase
    );
    return true;
}

b8 RendererRayTracingSystem::renderSoftOpaqueShadowResolveTail(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, const u32 frameIndex, const b8 hardwareShadowSupported){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const u32 groupSize = hardwareShadowSupported ? static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE) : static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 softGroupsX = ResolveWrappingRayTracingDispatchGroup(targets.width, targets.shadowReceiverFactor, groupSize);
    const u32 softGroupsY = ResolveWrappingRayTracingDispatchGroup(targets.height, targets.shadowReceiverFactor, groupSize);
    dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, deferredLightingResources, frameIndex, softGroupsX, softGroupsY, false, false, false, false, true, false, false, false, false, true, false);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingShadowVisibilityTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityOpaqueGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue)
        return false;

    payload.opaqueProduced = false;
    payload.opaqueFrameIndex = 0u;
    // A retry must never retain a timestamp whose producer command list is about to be replaced.
    if(payload.shadowVisibilityTiming.has_value()){
        payload.shadowVisibilityTiming.value().discardTiming();
        payload.shadowVisibilityTiming.reset();
    }
    if(payload.asyncTiming.has_value()){
        payload.asyncTiming.value().discardTiming();
        payload.asyncTiming.reset();
    }

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(queue->queueClass == Core::CommandQueue::Compute)
        payload.asyncTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_AsyncShadow, payload.graphics.getDevice(), commandList);
    payload.shadowVisibilityTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, payload.graphics.getDevice(), commandList);

    b8 opaqueRecorded = false;
    if(payload.prepared){
        const auto opaqueFrame = payload.hardwareShadowSupported
            ? payload.raytracingSystem.renderShadowVisibilityOpaque(commandList, payload.targets, payload.deferredLightingResources, payload.graphOwnsOpaqueTemporalMergeEntryStates, &payload.lightSpace)
            : payload.raytracingSystem.renderGpuBvhShadowVisibilityOpaque(commandList, payload.targets, payload.deferredLightingResources, payload.graphOwnsOpaqueTemporalMergeEntryStates, &payload.lightSpace)
        ;
        opaqueRecorded = opaqueFrame.has_value();
        if(opaqueFrame)
            payload.opaqueFrameIndex = *opaqueFrame;
    }
    if(!opaqueRecorded){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow producer failed; retaining all-lit visibility"));
        payload.raytracingSystem.clearShadowVisibility(commandList, payload.targets);
        // Later callbacks declare this output as UAV. Realign the tracker after the typed fallback clear.
        commandList.setTextureState(payload.targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        payload.shadowVisibilityTiming.value().discardTiming();
        payload.shadowVisibilityTiming.reset();
        if(payload.asyncTiming.has_value()){
            payload.asyncTiming.value().discardTiming();
            payload.asyncTiming.reset();
        }
        return true;
    }

    payload.opaqueProduced = true;
    // Both timestamp ranges end in the terminal fold callback, but their debug markers must close before this graph task's marker closes.
    const b8 visibilityMarkerFinished = Core::FinishSplitGpuTimingMarker(&payload.shadowVisibilityTiming);
    b8 asyncMarkerFinished = true;
    if(payload.asyncTiming.has_value())
        asyncMarkerFinished = Core::FinishSplitGpuTimingMarker(&payload.asyncTiming);
    if(!asyncMarkerFinished || !visibilityMarkerFinished){
        Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
        Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
        return false;
    }
    return true;
}

void ShadowVisibilityOpaqueGraphTask::Discarded(Payload& payload)noexcept{
    payload.opaqueProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
    Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityOpaqueFirstWaveletGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.opaqueResolveTiming.has_value()){
        payload.opaqueResolveTiming.value().discardTiming();
        payload.opaqueResolveTiming.reset();
    }
    payload.opaqueResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueResolve, payload.graphics.getDevice(), commandList);
    if(payload.raytracingSystem.renderSoftOpaqueShadowResolvePhase(
        commandList,
        payload.targets,
        payload.deferredLightingResources,
        payload.opaqueFrameIndex,
        payload.hardwareShadowSupported,
        payload.graphOwnsOpaqueTemporalMergeEntryStates,
        payload.deferWavelet ? SoftShadowOpaqueResolvePhase::TemporalOnly : SoftShadowOpaqueResolvePhase::TemporalAndWavelet
    )){
        if(payload.deferUpsample){
            payload.opaqueResolveTiming.value().finishTiming(commandList);
            payload.opaqueResolveTiming.reset();
            return true;
        }
        return Core::FinishSplitGpuTimingMarker(&payload.opaqueResolveTiming);
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow first wavelet failed; retaining all-lit visibility"));
    RayTracingShadowVisibilityTaskDetail::RecoverOpaqueSoftShadowAllLit(payload, commandList);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityOpaqueResolveTailGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;
    if(!payload.opaqueResolveTiming.has_value())
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.raytracingSystem.renderSoftOpaqueShadowResolveTail(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.hardwareShadowSupported)){
        payload.opaqueResolveTiming.value().finishTiming(commandList);
        payload.opaqueResolveTiming.reset();
        return true;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow resolve tail failed; retaining all-lit visibility"));
    RayTracingShadowVisibilityTaskDetail::RecoverOpaqueSoftShadowAllLit(payload, commandList);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftTraceGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    payload.transparentTraceProduced = false;
    if(!payload.opaqueProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.raytracingSystem.renderSoftTransparentShadowTrace(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, true, &payload.lightSpace)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace could not record; preserving opaque visibility"));
        return true;
    }
    payload.transparentTraceProduced = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftTemporalMergeGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    const auto recoverOpaqueTemporal = [&](){
        if(!payload.combinedTemporal)
            return true;
        return payload.raytracingSystem.renderSoftOpaqueShadowResolvePhase(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.hardwareShadowSupported, true, SoftShadowOpaqueResolvePhase::TemporalOnly);
    };
    if(!payload.transparentTraceProduced)
        return recoverOpaqueTemporal();
    if(payload.transparentResolveTiming.has_value()){
        payload.transparentResolveTiming.value().discardTiming();
        payload.transparentResolveTiming.reset();
    }
    payload.transparentResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentResolve, payload.graphics.getDevice(), commandList);
    const b8 merged = payload.combinedTemporal
        ? payload.raytracingSystem.renderSoftShadowCombinedTemporalMerge(commandList, payload.targets)
        : payload.raytracingSystem.renderSoftTransparentShadowTemporalMerge(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.graphOwnsTransparentTemporalMergeEntryStates)
    ;
    if(merged)
        return Core::FinishSplitGpuTimingMarker(&payload.transparentResolveTiming);

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow temporal merge failed; preserving opaque visibility"));
    payload.transparentTraceProduced = false;
    payload.transparentResolveTiming.value().discardTiming();
    payload.transparentResolveTiming.reset();
    return recoverOpaqueTemporal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftFirstWaveletGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;
    if(payload.combinedWavelet){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(payload.transparentTraceProduced && payload.transparentResolveTiming.has_value()){
            if(payload.raytracingSystem.renderSoftShadowCombinedWavelet(commandList, payload.targets))
                return true;
        }
        // Opaque temporal merge already completed. Recover only its first wavelet, without blending history twice.
        payload.transparentTraceProduced = false;
        Core::DiscardGpuTimingMeasure(&payload.transparentResolveTiming);
        return payload.raytracingSystem.renderSoftOpaqueShadowResolvePhase(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, true, true, SoftShadowOpaqueResolvePhase::WaveletOnly);
    }
    if(!payload.transparentTraceProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.startsTransparentResolveTiming){
        if(payload.transparentResolveTiming.has_value()){
            payload.transparentResolveTiming.value().discardTiming();
            payload.transparentResolveTiming.reset();
        }
        payload.transparentResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentResolve, payload.graphics.getDevice(), commandList);
    }
    else if(!payload.transparentResolveTiming.has_value()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow wavelet lost its temporal timing envelope; preserving opaque visibility"));
        payload.transparentTraceProduced = false;
        return true;
    }

    if(payload.raytracingSystem.renderSoftTransparentShadowFirstWavelet(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.graphOwnsTransparentWaveletInputBoundary)){
        if(payload.startsTransparentResolveTiming && !Core::FinishSplitGpuTimingMarker(&payload.transparentResolveTiming))
            return false;
        return true;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow first wavelet failed; preserving opaque visibility"));
    payload.transparentTraceProduced = false;
    if(payload.transparentResolveTiming.has_value()){
        payload.transparentResolveTiming.value().discardTiming();
        payload.transparentResolveTiming.reset();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftFoldGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue)
        return false;
    if(!payload.opaqueProduced)
        return true;
    if(!payload.shadowVisibilityTiming.has_value() || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value()))
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.transparentTraceProduced){
        if(!payload.transparentResolveTiming.has_value())
            return false;
        const b8 resolved = payload.combinedUpsample
            ? payload.raytracingSystem.renderSoftShadowTerminalUpsample(commandList, payload.targets, payload.deferredLightingResources, true)
            : payload.raytracingSystem.renderSoftTransparentShadowFold(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex)
        ;
        if(resolved){
            payload.transparentResolveTiming.value().finishTiming(commandList);
            payload.transparentResolveTiming.reset();
        }
        else{
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow resolve failed; preserving opaque visibility"));
            payload.transparentTraceProduced = false;
            payload.transparentResolveTiming.value().discardTiming();
            payload.transparentResolveTiming.reset();
        }
    }else{
        if(payload.transparentResolveTiming.has_value()){
            payload.transparentResolveTiming.value().discardTiming();
            payload.transparentResolveTiming.reset();
        }
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace or first wavelet failed; preserving opaque visibility"));
    }

    // Fusion deferred the opaque upsample, so a failed transparent phase must still publish opaque visibility.
    if(payload.combinedUpsample && !payload.transparentTraceProduced){
        if(!payload.raytracingSystem.renderSoftShadowTerminalUpsample(commandList, payload.targets, payload.deferredLightingResources, false))
            return false;
    }

    payload.shadowVisibilityTiming.value().finishTiming(commandList);
    payload.shadowVisibilityTiming.reset();
    if(payload.asyncTiming.has_value()){
        payload.asyncTiming.value().finishTiming(commandList);
        payload.asyncTiming.reset();
    }
    return true;
}

void ShadowTransparentSoftFoldGraphTask::Discarded(Payload& payload)noexcept{
    payload.transparentTraceProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
    Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
    Core::DiscardGpuTimingMeasure(&payload.transparentResolveTiming);
    payload.raytracingSystem.discardSoftShadowTemporalHistory();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    const GraphOwnedAdaptiveShadowPlan& adaptivePlan = payload.graphOwnedAdaptivePlan;
    const GraphOwnedAdaptiveShadowPlan* const graphOwnedAdaptivePlan = adaptivePlan.enabled ? &adaptivePlan : nullptr;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    Optional<Core::GpuTimingMeasure> asyncTiming;
    if(queue->queueClass == Core::CommandQueue::Compute)
        asyncTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_AsyncShadow, payload.graphics.getDevice(), commandList);

    b8 shadowVisibilityWritten = false;
    if(payload.prepared && payload.hardwareShadowSupported){
        shadowVisibilityWritten = payload.raytracingSystem.renderShadowVisibility(commandList, payload.targets, payload.deferredLightingResources).has_value();
        if(!shadowVisibilityWritten)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: ray-traced shadow visibility pass failed"));
    }
    else if(payload.prepared)
        shadowVisibilityWritten = payload.raytracingSystem.renderGpuBvhShadowVisibility(commandList, payload.targets, payload.deferredLightingResources, false, false, false, graphOwnedAdaptivePlan).has_value();
    // The preceding typed white clear remains the all-lit result when no producer records.

    if(asyncTiming){
        asyncTiming->finishTiming(commandList);
        asyncTiming.reset();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowVisibilityTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& prepared,
    const b8 hardwareShadowSupported,
    const GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .prepared = prepared,
            .graphOwnedAdaptivePlan = graphOwnedAdaptivePlan,
            .hardwareShadowSupported = hardwareShadowSupported,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowTransparentSoftTemporalMergeTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& opaqueProduced,
    const u32& opaqueFrameIndex,
    const b8 graphOwnsTransparentTemporalMergeEntryStates,
    const b8 combinedTemporal,
    const b8 hardwareShadowSupported,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    b8& transparentTraceProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTemporalMergeGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTemporalMergeGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .graphOwnsTransparentTemporalMergeEntryStates = graphOwnsTransparentTemporalMergeEntryStates,
            .combinedTemporal = combinedTemporal,
            .hardwareShadowSupported = hardwareShadowSupported,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowTransparentSoftFirstWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& opaqueProduced,
    const u32& opaqueFrameIndex,
    const b8 graphOwnsTransparentWaveletInputBoundary,
    const b8 startsTransparentResolveTiming,
    const b8 combinedWavelet,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    b8& transparentTraceProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFirstWaveletGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFirstWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .graphOwnsTransparentWaveletInputBoundary = graphOwnsTransparentWaveletInputBoundary,
            .startsTransparentResolveTiming = startsTransparentResolveTiming,
            .combinedWavelet = combinedWavelet,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowTransparentSoftFoldTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& opaqueProduced,
    const u32& opaqueFrameIndex,
    const b8 combinedUpsample,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    b8& transparentTraceProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFoldGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFoldGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .combinedUpsample = combinedUpsample,
        });
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareShadowTransparentSoftTraceTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8& opaqueProduced,
    const u32& opaqueFrameIndex,
    const LightSpaceShadowSnapshot* const lightSpace,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    b8& transparentTraceProduced
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTraceGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTraceGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .lightSpace = lightSpace ? *lightSpace : LightSpaceShadowSnapshot{},
            .timingTicket = timingTicket,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .transparentTraceProduced = transparentTraceProduced,
        });
}

b8 RendererRayTracingSystem::renderSoftTransparentShadowTrace(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const b8 graphOwnsOpaqueToTransparentBoundary,
    const LightSpaceShadowSnapshot* const lightSpace
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const b8 composeCsg = lightSpace && lightSpace->ready && (lightSpace->push.csgFlags & NWB_CSG_SHADOW_FLAG_HW_COMPOSE) != 0u;
    const b8 replaceTransparent = composeCsg && (lightSpace->push.csgFlags & NWB_CSG_SHADOW_FLAG_NO_ORDINARY_TRANSPARENT) != 0u;
    if(lightSpace && lightSpace->ready && (!composeCsg || replaceTransparent)){
        NWB_ASSERT(graphOwnsOpaqueToTransparentBoundary);
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTrace, m_graphics.getDevice(), commandList);

        const u32 sampleCount = transparentShadowSampleCount();
        if(!RecordLightSpaceResolve(*lightSpace, frameIndex, sampleCount, targets.bindless.transparentSoftHalfStorage.slot(), true, commandList, m_graphics.getDevice().getDescriptorHeap(), m_graphics.gpuTiming(), *targets.transparentSoftHalf))
            return false;
        reportTransparentShadowSampling(sampleCount);
        return true;
    }
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, deferredLightingResources, frameIndex, groups.x, groups.y, false, false, true, false, false, graphOwnsOpaqueToTransparentBoundary, false, false, false, false, false);
    if(composeCsg){
        commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
        commandList.commitBarriers();
        if(!RecordLightSpaceResolve(
            *lightSpace, frameIndex, transparentShadowSampleCount(),
            targets.bindless.transparentSoftHalfStorage.slot(), true,
            commandList, m_graphics.getDevice().getDescriptorHeap(), m_graphics.gpuTiming(), *targets.transparentSoftHalf
        ))
            return false;
    }
    if(!hardwareTransparentShadowReady())
        reportSoftwareShadowTraversal(targets);
    return true;
}

b8 RendererRayTracingSystem::renderSoftTransparentShadowTemporalMerge(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const b8 graphOwnsTransparentTemporalMergeEntryStates
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || !m_rayTracingState.m_softTransparentTemporalReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        groups.x,
        groups.y,
        false,
        false,
        false,
        false,
        false,
        false,
        true,
        false,
        graphOwnsTransparentTemporalMergeEntryStates,
        false,
        false,
        false,
        false,
        true
    );
    return true;
}

b8 RendererRayTracingSystem::renderSoftTransparentShadowFirstWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const b8 graphOwnsTransparentWaveletInputBoundary
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, deferredLightingResources, frameIndex, groups.x, groups.y, false, false, false, true, false, false, graphOwnsTransparentWaveletInputBoundary, false, false, false, false, false, true, false);
    return true;
}

b8 RendererRayTracingSystem::renderSoftTransparentShadowFold(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, const u32 frameIndex){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return false;
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, deferredLightingResources, frameIndex, groups.x, groups.y, false, false, false, false, false, false, false, false, false, false, false, true);
    return true;
}

void RendererRayTracingSystem::reportSoftwareShadowTraversal(const DeferredFrameTargets& targets){
    LogSoftwareShadowTraversalDispatchIfNeeded(
        targets.width,
        targets.height,
        m_rayTracingState.m_sceneBvhInstanceCount,
        m_rayTracingState.m_swShadowDispatchLogged
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u32> RendererRayTracingSystem::renderShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 splitOpaqueSoftResolve,
    const LightSpaceShadowSnapshot* const lightSpace
){
    u32 producedFrameIndex = 0u;
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    NWB_ASSERT(deferredLightingResources.valid());
    if(!targets.shadowVisibility)
        return MakeUnexpected(Failure{});
    if(!m_rayTracingState.m_tlas || !m_rayTracingState.m_shadowPipeline)
        return MakeUnexpected(Failure{});

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !m_rayTracingState.m_tlasHeapHandle.valid()
        || !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow trace heap resources are incomplete"));
        return MakeUnexpected(Failure{});
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, m_graphics.getDevice(), commandList);

    // Hardware tracing shares the half-resolution soft-shadow resolve when available.
    if(m_rayTracingState.m_softShadowReady && m_rayTracingState.m_shadowSoftPipeline && m_rayTracingState.m_softShadowSlotMask != 0u){
        const u32 softGroupsX = ResolveWrappingRayTracingDispatchGroup(targets.width, targets.shadowReceiverFactor, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));
        const u32 softGroupsY = ResolveWrappingRayTracingDispatchGroup(targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));


        // Resolve reuses the trace outputs as UAV/SRV scratch.
        enableSoftShadowResolveBarriers(commandList, targets);

        // Advance the jitter sequence once for the primary shadow producer.
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;

        {
            Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

            Core::ComputeState softState;
            softState.setPipeline(m_rayTracingState.m_shadowSoftPipeline.get());
            commandList.setComputeState(softState);
            heap.bindCompute(commandList, *m_rayTracingState.m_shadowSoftPipeline.get(), m_rayTracingState.m_tlasHeapHandle);

            const ShadowRqSoftPushConstants softPush = MakeShadowRqSoftPushConstants(
                targets.width,
                targets.height,
                frameIndex,
                targets.bindless.gbufferWorldPosition.slot(),
                targets.bindless.gbufferNormal.slot(),
                targets.bindless.gbufferDepth.slot(),
                targets.bindless.slotsBufferDescriptor.slot(),
                targets.bindless.shadowSoftHalfAStorage.slot(),
                softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP,
                targets.shadowReceiverFactor
            );
            commandList.setPushConstants(&softPush, sizeof(softPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }

        if(lightSpace && lightSpace->ready){
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            if(!RecordLightSpaceResolve(
                *lightSpace,
                frameIndex, softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP,
                targets.bindless.shadowSoftHalfAStorage.slot(), false,
                commandList, heap, m_graphics.gpuTiming(), *targets.shadowSoftHalfA
            ))
                return MakeUnexpected(Failure{});
            LightSpaceShadowLogReporter{}.reportCsgDispatch(
                true,
                true,
                lightSpace->push.instanceCount,
                m_lightSpaceShadow.m_csgDispatchLogged
            );
        }

        // Split resolve declares the trace fence; unsplit soft resolve phases retain their internal UAV fence.
        dispatchPrimarySoftShadowResolve(commandList, targets, deferredLightingResources, frameIndex, softGroupsX, softGroupsY, splitOpaqueSoftResolve, splitSoftTransparentFold, graphOwnsOpaqueTemporalMergeEntryStates);
        return producedFrameIndex;
    }

    // Full-resolution inline-RayQuery fallback.

    {
        Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

        Core::ComputeState shadowState;
        shadowState.setPipeline(m_rayTracingState.m_shadowPipeline.get());
        commandList.setComputeState(shadowState);
        heap.bindCompute(commandList, *m_rayTracingState.m_shadowPipeline.get(), m_rayTracingState.m_tlasHeapHandle);
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;
        const ShadowRqPushConstants shadowPush = MakeShadowRqPushConstants(
            frameIndex,
            targets.bindless.gbufferWorldPosition.slot(),
            targets.bindless.gbufferNormal.slot(),
            targets.bindless.gbufferDepth.slot(),
            targets.bindless.slotsBufferDescriptor.slot(),
            targets.bindless.shadowVisibilityStorage.slot()
        );
        commandList.setPushConstants(&shadowPush, sizeof(shadowPush));
        commandList.dispatch(DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)), DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)), 1u);
    }
    return producedFrameIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::dispatchSoftShadowDenoiseAndTransparentFold(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const u32 softGroupsX,
    const u32 softGroupsY,
    const b8 dispatchOpaqueGeometry,
    const b8 dispatchOpaqueResolve,
    const b8 dispatchTransparentTrace,
    const b8 dispatchTransparentResolve,
    const b8 graphOwnsOpaqueGeometryToResolveBoundary,
    const b8 graphOwnsOpaqueToTransparentBoundary,
    const b8 graphOwnsTransparentTraceToResolveBoundary,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 graphOwnsTransparentTemporalMergeEntryStates,
    const b8 dispatchOpaqueResolveTail,
    const b8 graphOwnsOpaqueTraceToFirstWaveletBoundary,
    const b8 dispatchTransparentResolveTail,
    const b8 splitTransparentResolve,
    const b8 dispatchTransparentTemporalMerge,
    const SoftShadowOpaqueResolvePhase::Enum opaquePhase
){
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.sceneShading, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftGeometryStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfBStorage, Core::GpuDescriptorClass::StorageImage)
    )
        return;

    const u32 softHalfWidth = DivideUpWrapping(targets.width, targets.shadowReceiverFactor);
    const u32 softHalfHeight = DivideUpWrapping(targets.height, targets.shadowReceiverFactor);

    if(dispatchOpaqueGeometry){
        // Graph declares downsample inputs; later soft-shadow transitions stay local.
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);

        {
            Core::GpuTimingMeasure geometryTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowGeometryDownsample, m_graphics.getDevice(), commandList);

            const ShadowGeometryDownsamplePushConstants geometryPush = MakeShadowGeometryDownsamplePushConstants(
                targets.width,
                targets.height,
                softHalfWidth,
                softHalfHeight,
                targets.bindless.gbufferWorldPosition.slot(),
                targets.bindless.gbufferNormal.slot(),
                targets.bindless.gbufferDepth.slot(),
                targets.bindless.shadowSoftGeometryStorage.slot(),
                targets.bindless.sceneShading.slot(),
                targets.shadowReceiverFactor
            );
            commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_shadowGeometryDownsamplePipeline));
            heap.bindCompute(commandList, *m_rayTracingState.m_shadowGeometryDownsamplePipeline.get());
            commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }
    }

    if(!dispatchOpaqueResolve && !dispatchOpaqueResolveTail && !dispatchTransparentTrace && !dispatchTransparentResolve && !dispatchTransparentResolveTail && !dispatchTransparentTemporalMerge)
        return;

    const u32 slotRangeCount = ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask);
    if(slotRangeCount == 0u)
        return;

    const b8 frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    const b8 opaqueTemporalActive = m_rayTracingState.m_softShadowTemporalReady;
    const b8 temporalHistoryReadable = softShadowTemporalHistoryUsable();

    SoftShadowTemporalMergeRecorder mergeRecorder(
        m_rayTracingState, slotRangeCount, softHalfWidth, softHalfHeight, softGroupsX, softGroupsY, temporalHistoryReadable,
        commandList, targets, heap
    );

    if(dispatchOpaqueResolve || dispatchOpaqueResolveTail){
        const ShadowReprojectMergeHeapResources opaqueMerge = frontIsA
            ? ShadowReprojectMergeHeapResources{
                targets.shadowSoftHalfA.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(),
                targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistA.slot(), targets.bindless.shadowMomentsA.slot(),
                targets.bindless.shadowHistBStorage.slot(), targets.bindless.shadowMomentsBStorage.slot()
            }
            : ShadowReprojectMergeHeapResources{
                targets.shadowSoftHalfA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(),
                targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistB.slot(), targets.bindless.shadowMomentsB.slot(),
                targets.bindless.shadowHistAStorage.slot(), targets.bindless.shadowMomentsAStorage.slot()
            }
        ;
        // Merge writes the NEXT pair; the wavelet must read that same pair, never stale A.
        if(dispatchOpaqueResolve && opaqueTemporalActive && opaquePhase != SoftShadowOpaqueResolvePhase::WaveletOnly){
            Core::GpuTimingMeasure opaqueTemporalTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTemporal, m_graphics.getDevice(), commandList);

            // The graph owns the selected history/moments plus stable previous-geometry/world reads.
            // The geometry downsample above still needs this callback's local UAV-to-SRV transition before the opaque merge samples it.
            mergeRecorder.record(opaqueMerge, graphOwnsOpaqueTraceToFirstWaveletBoundary, graphOwnsOpaqueGeometryToResolveBoundary, graphOwnsOpaqueTemporalMergeEntryStates, graphOwnsOpaqueTemporalMergeEntryStates);
        }

        if(opaquePhase != SoftShadowOpaqueResolvePhase::TemporalOnly){
            // The first wavelet reads this frame's trace or temporal merge directly, preserving its exact filtering input.
            const OpaqueSoftShadowResolveOptions opaqueOptions{
                .frontIsA = frontIsA,
                .temporalActive = opaqueTemporalActive,
                .graphOwnsGeometryEntry = graphOwnsOpaqueGeometryToResolveBoundary,
                .graphOwnsTraceToFirstWavelet = graphOwnsOpaqueTraceToFirstWaveletBoundary,
                .dispatchResolve = dispatchOpaqueResolve,
                .dispatchResolveTail = dispatchOpaqueResolveTail,
                .phase = opaquePhase,
            };
            const SoftShadowResolveDispatch opaqueDispatch = SoftShadowResolveDispatchBuilder::Opaque(opaqueOptions, m_rayTracingState, targets);
            if(dispatchOpaqueResolve && dispatchOpaqueResolveTail){
                Core::GpuTimingMeasure opaqueResolveTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueResolve, m_graphics.getDevice(), commandList);

                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, opaqueDispatch);
            }
            else if(dispatchOpaqueResolve)
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, opaqueDispatch, true, false);
            else
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, opaqueDispatch, false, true);
        }
    }

    if(dispatchTransparentTrace && m_rayTracingState.m_softTransparentReady){
        {
            Core::GpuTimingMeasure transparentTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTrace, m_graphics.getDevice(), commandList);

            if(hardwareTransparentShadowReady())
                dispatchHardwareTransparentShadow(commandList, targets, frameIndex);
            else{
                const SoftwareTransparentShadowTraceInputs traceInputs{
                    .frameIndex = frameIndex,
                    .sampleCount = transparentShadowSampleCount(),
                    .groupsX = softGroupsX,
                    .groupsY = softGroupsY,
                    .graphOwnsOpaqueToTransparentBoundary = graphOwnsOpaqueToTransparentBoundary,
                    .temporalHistoryReadable = temporalHistoryReadable,
                    .historyFrontIsA = frontIsA,
                };
                SoftwareTransparentShadowTraceRecorder::Record(traceInputs, m_rayTracingState, commandList, targets, heap);
                reportTransparentShadowSampling(traceInputs.sampleCount);
            }
        }
    }

    const b8 dispatchTransparentWavelet = dispatchTransparentResolve || dispatchTransparentResolveTail;
    const b8 dispatchTransparentMerge = dispatchTransparentTemporalMerge || (dispatchTransparentResolve && !splitTransparentResolve);
    if((dispatchTransparentMerge || dispatchTransparentWavelet) && m_rayTracingState.m_softTransparentReady){
        const b8 transparentTemporalActive = m_rayTracingState.m_softTransparentTemporalReady;
        const ShadowReprojectMergeHeapResources transparentMerge = frontIsA
            ? ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistA.slot(), targets.bindless.transparentMomentsA.slot(),
                targets.bindless.transparentHistBStorage.slot(), targets.bindless.transparentMomentsBStorage.slot()
            }
            : ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistB.slot(), targets.bindless.transparentMomentsB.slot(),
                targets.bindless.transparentHistAStorage.slot(), targets.bindless.transparentMomentsAStorage.slot()
            }
        ;
        // Keep the RGB wavelet's temporal variance paired with the history just emitted by the transparent merge.
        if(dispatchTransparentMerge && transparentTemporalActive){
            Core::GpuTimingMeasure transparentTemporalTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTemporal, m_graphics.getDevice(), commandList);

            mergeRecorder.record(transparentMerge, graphOwnsTransparentTraceToResolveBoundary, graphOwnsTransparentTemporalMergeEntryStates, graphOwnsTransparentTemporalMergeEntryStates, graphOwnsTransparentTemporalMergeEntryStates);
            m_rayTracingState.m_transparentShadowSamplingHistory.record(m_rayTracingState.m_softShadowSlotMask);
        }

        if(dispatchTransparentWavelet){
            const TransparentSoftShadowResolveOptions transparentOptions{
                .frontIsA = frontIsA,
                .temporalActive = transparentTemporalActive,
                .dispatchResolve = dispatchTransparentResolve,
                .dispatchResolveTail = dispatchTransparentResolveTail,
                .splitResolve = splitTransparentResolve,
                .dispatchTemporalMerge = dispatchTransparentTemporalMerge,
                .graphOwnsTraceToResolve = graphOwnsTransparentTraceToResolveBoundary,
            };
            const SoftShadowResolveDispatch transparentDispatch = SoftShadowResolveDispatchBuilder::Transparent(transparentOptions, m_rayTracingState, targets);
            if(dispatchTransparentResolve && !splitTransparentResolve){
                Core::GpuTimingMeasure transparentResolveTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentResolve, m_graphics.getDevice(), commandList);

                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, transparentDispatch);
            }
            else if(dispatchTransparentResolve)
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, transparentDispatch, true, false);
            else
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, transparentDispatch, false, true);
        }
    }

    // Do not mutate the target-generation handles while the sibling caustics and surfel-GI workers can still validate targets.bindless.
    // RendererFramePipeline finalizes this pending CPU-side swap only after its complete ordered Graphics submission succeeds.
    if(((dispatchTransparentResolve && !splitTransparentResolve) || dispatchTransparentResolveTail) && m_rayTracingState.m_softShadowTemporalReady)
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::softShadowTemporalHistoryUsable()const noexcept{
    return SoftShadowTemporalHistoryUsable(m_rayTracingState.m_softShadowTemporalReady, m_rayTracingState.m_prevWorldToClipValid, m_rayTracingState.m_softShadowTemporalSeeded);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_softshadow_pipelines{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 EnsureResolveChannel(
    const Core::BindingLayoutHandle& bindingLayout,
    const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::* member,
    RendererShaderSystem& shaderSystem,
    Core::GraphicsBackend::Device& device,
    SoftShadowResolveChannelState& channel
){
    struct StageRequest{
        AStringView m_variant;
        SoftShadowResolveStageState& m_state;
    };
    const Array<StageRequest, 2u> stages = {{
        { s_ShadowResolveWaveletVariant, channel.m_wavelet },
        { s_ShadowResolveUpsampleVariant, channel.m_upsample },
    }};
    static_assert(NWB_SHADOW_RESOLVE_STAGE_WAVELET == 1u && NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE == 2u);
    for(const StageRequest& stage : stages){
        if(stage.m_state.m_pipeline)
            continue;
        if(
            !shaderSystem.loadShader<ComputeShader>(member, stage.m_variant, "ECSRender_SoftShadowResolve", stage.m_state.m_shader)
            || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, stage.m_state.m_shader, bindingLayout, false, stage.m_state.m_pipeline)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create specialized shadow resolve pipeline"));
            channel.m_failed = true;
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::finalizeSoftShadowTemporalHistory(DeferredFrameTargets& targets)noexcept{
    if(!m_rayTracingState.m_softShadowTemporalHistoryAdvancePending)
        return;

    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    swapSoftShadowTemporalHistory(targets);
}

void RendererRayTracingSystem::discardSoftShadowTemporalHistory()noexcept{
    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    m_rayTracingState.m_transparentShadowSamplingHistory.discardPending();
}

b8 ShadowPipelineSet::ensureSoftShadowResolvePipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_scalar.m_wavelet.m_pipeline && resolve.m_scalar.m_upsample.m_pipeline)
        return true;
    if(resolve.m_scalar.m_failed)
        return false;

    auto& device = m_graphics.getDevice();
    if(!device.getDescriptorHeap().isInitialized())
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(ShadowResolvePushConstants), Core::ShaderType::Compute, resolve.m_bindingLayout)){
        resolve.m_scalar.m_failed = true;
        return false;
    }
    return __hidden_softshadow_pipelines::EnsureResolveChannel(resolve.m_bindingLayout, &ShadowSubsystem::shadowResolveCs, m_shaderSystem, device, resolve.m_scalar);
}

b8 ShadowPipelineSet::ensureShadowGeometryDownsamplePipeline(){
    if(m_softShadow.m_shadowGeometryDownsamplePipeline)
        return true;
    if(m_softShadow.m_shadowGeometryDownsamplePipelineFailed)
        return false;

    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &ShadowSubsystem::shadowGeometryDownsampleCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_ShadowGeometryDownsample",
        sizeof(ShadowGeometryDownsamplePushConstants),
        false,
        m_softShadow.m_shadowGeometryDownsampleBindingLayout,
        m_softShadow.m_shadowGeometryDownsampleShader,
        m_softShadow.m_shadowGeometryDownsamplePipeline
    )){
        m_softShadow.m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

b8 ShadowPipelineSet::ensureSoftTransparentResolvePipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_rgb.m_wavelet.m_pipeline && resolve.m_rgb.m_upsample.m_pipeline)
        return true;
    if(resolve.m_rgb.m_failed || !resolve.m_bindingLayout)
        return false;

    auto& device = m_graphics.getDevice();
    if(!device.getDescriptorHeap().isInitialized())
        return false;
    return __hidden_softshadow_pipelines::EnsureResolveChannel(resolve.m_bindingLayout, &ShadowSubsystem::shadowResolveRgbCs, m_shaderSystem, device, resolve.m_rgb);
}

// Both trace backends publish the same independent channel textures and use the same optional resolve pipelines.
void ShadowPipelineSet::prepareSoftCombinedResolvePipelines(){
    if(!m_softShadow.m_softShadowReady || !m_softShadow.m_softTransparentReady)
        return;
    if(!ensureSoftCombinedUpsamplePipeline())
        m_shadow.m_softShadowResolve.m_combinedUpsampleFailed = true;
    if(!ensureSoftCombinedWaveletPipeline())
        m_shadow.m_softShadowResolve.m_combinedWaveletFailed = true;
    if(!ensureSoftCombinedTemporalPipeline())
        m_shadow.m_softShadowResolve.m_combinedTemporalFailed = true;
}

void RendererRayTracingSystem::dispatchSoftShadowResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 slotStart,
    const u32 slotCount,
    const SoftShadowResolveDispatch& dispatch,
    const b8 dispatchFirstWavelet,
    const b8 dispatchTail
){
    NWB_ASSERT(dispatch.waveletPipeline && dispatch.upsamplePipeline);
    NWB_ASSERT(dispatch.visibilityTexture);
    const u32 halfWidth = DivideUpWrapping(targets.width, targets.shadowReceiverFactor);
    const u32 halfHeight = DivideUpWrapping(targets.height, targets.shadowReceiverFactor);
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const DeferredBindlessFrameResources& bindless = targets.bindless;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();

    const auto runPass = [&](
        const SoftShadowResolvePassResources& resources, const u32 stepWidth, const ShadowResolveStage::Enum stage, const u32 groupsX, const u32 groupsY, const b8 graphOwnsInputColorState = false, const b8 graphOwnsOutputState = false
    ){
        NWB_ASSERT(resources.inputColorTexture && resources.momentsTexture && resources.outputTexture);
        switch(stage){
            case ShadowResolveStage::Wavelet:
                if(!dispatch.graphOwnsWaveletGeometryEntryState)
                    commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsInputColorState)
                    commandList.setTextureState(resources.inputColorTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(dispatch.temporalMomentsValid && !dispatch.graphOwnsWaveletMomentsEntryState)
                    commandList.setTextureState(resources.momentsTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsOutputState)
                    commandList.setTextureState(resources.outputTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setEnableUavBarriersForTexture(resources.outputTexture, true);
                break;
            case ShadowResolveStage::Upsample:
                if(!dispatch.graphOwnsUpsampleStaticEntryStates){
                    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
                }
                if(!graphOwnsInputColorState)
                    commandList.setTextureState(resources.inputColorTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsOutputState)
                    commandList.setTextureState(dispatch.visibilityTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setEnableUavBarriersForTexture(dispatch.visibilityTexture, true);
                break;
        }
        commandList.commitBarriers();

        const ShadowResolvePushConstants push = MakeShadowResolvePassPushConstants(
            targets.shadowReceiverFactor,
            targets.width,
            targets.height,
            halfWidth,
            halfHeight,
            stepWidth,
            stage,
            slotStart,
            slotCount,
            dispatch.temporalMomentsValid,
            dispatch.fold,
            bindless.shadowSoftGeometry.slot(),
            bindless.gbufferDepth.slot(),
            bindless.gbufferWorldPosition.slot(),
            bindless.gbufferNormal.slot(),
            resources.inputColor,
            resources.moments,
            resources.outputStorage,
            dispatch.visibilityStorage,
            dispatch.sceneShading
        );

        Core::ComputePipeline& pipeline = stage == ShadowResolveStage::Upsample ? *dispatch.upsamplePipeline : *dispatch.waveletPipeline;
        Core::ComputeState state;
        state.setPipeline(&pipeline);
        commandList.setComputeState(state);
        heap.bindCompute(commandList, pipeline);
        commandList.setPushConstants(&push, sizeof(push));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    static_assert((NWB_SHADOW_RESOLVE_PASS_COUNT % 2) == 1, "opaque resolve pass count must be odd");
    static_assert((NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT % 2) == 1, "transparent resolve pass count must be odd");
    NWB_ASSERT(dispatch.waveletPassCount != 0u && (dispatch.waveletPassCount % 2u) == 1u);
    NWB_ASSERT(dispatchFirstWavelet || dispatchTail);

    if(dispatchFirstWavelet)
        runPass(dispatch.firstWaveletResources, 1u, ShadowResolveStage::Wavelet, halfGroupsX, halfGroupsY, dispatch.graphOwnsFirstWaveletInputState, dispatch.graphOwnsFirstWaveletOutputState);
    if(!dispatchTail)
        return;

    b8 sourceIsHalfA = dispatch.firstWaveletWritesHalfA;
    for(u32 pass = 1u; pass < dispatch.waveletPassCount; ++pass){
        const SoftShadowResolvePassResources& nextWaveletResources = sourceIsHalfA ? dispatch.outputHalfBResources : dispatch.outputHalfAResources;
        runPass(nextWaveletResources, 1u << pass, ShadowResolveStage::Wavelet, halfGroupsX, halfGroupsY);
        sourceIsHalfA = !sourceIsHalfA;
    }
    NWB_ASSERT(dispatch.upsampleResources.inputColorTexture == (dispatch.waveletPassCount == 1u ? dispatch.firstWaveletResources : (sourceIsHalfA ? dispatch.outputHalfAResources : dispatch.outputHalfBResources)).outputTexture);
    runPass(dispatch.upsampleResources, 1u, ShadowResolveStage::Upsample, fullGroupsX, fullGroupsY, dispatch.graphOwnsUpsampleInputColorEntryState, dispatch.graphOwnsUpsampleVisibilityOutputState);
}

b8 ShadowPipelineSet::ensureShadowReprojectMergePipeline(){
    if(m_softShadow.m_shadowReprojectMergePipeline)
        return true;
    if(m_softShadow.m_shadowReprojectMergePipelineFailed)
        return false;

    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &ShadowSubsystem::shadowReprojectMergeCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_SoftShadowReprojectMerge",
        sizeof(ShadowReprojectMergePushConstants),
        false,
        m_softShadow.m_shadowReprojectMergeBindingLayout,
        m_softShadow.m_shadowReprojectMergeShader,
        m_softShadow.m_shadowReprojectMergePipeline
    )){
        m_softShadow.m_shadowReprojectMergePipelineFailed = true;
        return false;
    }
    return true;
}

void RendererRayTracingSystem::swapSoftShadowTemporalHistory(DeferredFrameTargets& targets)noexcept{
    if(!m_rayTracingState.m_softShadowTemporalReady)
        return;

    const auto acceptedWorldToClip = m_meshSystem.snapshotAcceptedMeshViewWorldToClip();
    if(acceptedWorldToClip){
        m_rayTracingState.m_prevWorldToClip = *acceptedWorldToClip;
        m_rayTracingState.m_prevWorldToClipValid = true;
    }
    if(m_rayTracingState.m_softTransparentTemporalReady && !hardwareTransparentShadowReady())
        m_rayTracingState.m_softwareTransparentSampling.m_history.accept();
    else
        m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    m_rayTracingState.m_transparentShadowSamplingHistory.accept();
    m_rayTracingState.m_softShadowTemporalSeeded = true;
    Swap(targets.shadowSoftGeometry, targets.shadowSoftGeometryPrev);
    Swap(targets.bindless.shadowSoftGeometry, targets.bindless.shadowSoftGeometryPrev);
    Swap(targets.bindless.shadowSoftGeometryStorage, targets.bindless.shadowSoftGeometryPrevStorage);
    m_rayTracingState.m_softShadowHistoryFrontIsA ^= 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderSoftShadowCombinedTemporalMerge(Core::CommandList& commandList, DeferredFrameTargets& targets){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(
        !resolve.m_combinedTemporal.m_pipeline
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || !m_rayTracingState.m_softShadowTemporalReady || !m_rayTracingState.m_softTransparentTemporalReady
        || !targets.bindless.valid() || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;

    const b8 frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    const Array<Core::Texture*, 4> outputs = {
        frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get(),
        frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get(),
        frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get(),
        frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get(),
    };
    // This graph-only pass inherits both raw inputs and all four selected history/moment output states.
    for(Core::Texture* const output : outputs)
        commandList.setEnableUavBarriersForTexture(output, true);
    commandList.commitBarriers();

    const DeferredBindlessFrameResources& bindless = targets.bindless;
    const ShadowCombinedTemporalPushConstants push = MakeShadowCombinedTemporalPushConstants(
        LoadFloat(m_rayTracingState.m_prevWorldToClip),
        targets.shadowReceiverFactor,
        ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask),
        softShadowTemporalHistoryUsable(),
        bindless.shadowSoftGeometry.slot(),
        bindless.shadowSoftGeometryPrev.slot(),
        bindless.gbufferWorldPosition.slot(),
        bindless.shadowSoftHalfA.slot(),
        frontIsA ? bindless.shadowHistA.slot() : bindless.shadowHistB.slot(),
        frontIsA ? bindless.shadowMomentsA.slot() : bindless.shadowMomentsB.slot(),
        frontIsA ? bindless.shadowHistBStorage.slot() : bindless.shadowHistAStorage.slot(),
        frontIsA ? bindless.shadowMomentsBStorage.slot() : bindless.shadowMomentsAStorage.slot(),
        bindless.transparentSoftHalf.slot(),
        frontIsA ? bindless.transparentHistA.slot() : bindless.transparentHistB.slot(),
        frontIsA ? bindless.transparentMomentsA.slot() : bindless.transparentMomentsB.slot(),
        frontIsA ? bindless.transparentHistBStorage.slot() : bindless.transparentHistAStorage.slot(),
        frontIsA ? bindless.transparentMomentsBStorage.slot() : bindless.transparentMomentsAStorage.slot()
    );
    Core::ComputeState state;
    state.setPipeline(resolve.m_combinedTemporal.m_pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resolve.m_combinedTemporal.m_pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowCombinedTemporal, m_graphics.getDevice(), commandList);

    commandList.dispatch(
        DivideUp(DivideUp(targets.width, targets.shadowReceiverFactor), static_cast<u32>(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE)),
        DivideUp(DivideUp(targets.height, targets.shadowReceiverFactor), static_cast<u32>(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE)),
        1u
    );
    m_rayTracingState.m_transparentShadowSamplingHistory.record(m_rayTracingState.m_softShadowSlotMask);
    return true;
}

b8 ShadowPipelineSet::ensureSoftCombinedTemporalPipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_combinedTemporal.m_pipeline)
        return true;
    if(resolve.m_combinedTemporalFailed)
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &ShadowSubsystem::shadowReprojectMergeCombinedCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CombinedShadowTemporal",
        sizeof(ShadowCombinedTemporalPushConstants),
        false,
        resolve.m_combinedTemporalBindingLayout,
        resolve.m_combinedTemporal.m_shader,
        resolve.m_combinedTemporal.m_pipeline
    )){
        resolve.m_combinedTemporalFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderSoftShadowTerminalUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, const b8 transparentReady){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    const Core::ComputePipelineHandle& pipeline = transparentReady ? resolve.m_combinedUpsample.m_pipeline : resolve.m_scalar.m_upsample.m_pipeline;
    if(
        !pipeline
        || !m_rayTracingState.m_softShadowReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
        || NWB_SHADOW_RESOLVE_PASS_COUNT != 1u
        || NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT != 1u
        || !deferredLightingResources.valid()
        || !targets.bindless.valid()
        || !targets.depth
        || !targets.worldPosition
        || !targets.normal
        || !targets.shadowSoftHalfB
        || !targets.shadowSoftGeometry
        || !targets.shadowVisibility
        || (transparentReady && (!m_rayTracingState.m_softTransparentReady || !targets.shadowSoftHalfA))
    )
        return false;

    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);
    commandList.commitBarriers();

    const DeferredBindlessFrameResources& bindless = targets.bindless;
    const ShadowResolvePushConstants push = MakeShadowTerminalUpsamplePushConstants(
        targets.width,
        targets.height,
        DivideUp(targets.width, static_cast<u32>(targets.shadowReceiverFactor)),
        DivideUp(targets.height, static_cast<u32>(targets.shadowReceiverFactor)),
        ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask),
        bindless.shadowSoftGeometry.slot(),
        bindless.gbufferDepth.slot(),
        bindless.gbufferWorldPosition.slot(),
        bindless.gbufferNormal.slot(),
        transparentReady ? bindless.shadowSoftHalfA.slot() : bindless.shadowSoftHalfB.slot(),
        bindless.shadowVisibilityStorage.slot(),
        bindless.shadowVisibilityStorage.slot(),
        bindless.sceneShading.slot(),
        bindless.shadowSoftHalfB.slot(),
        targets.shadowReceiverFactor
    );
    Core::ComputeState state;
    state.setPipeline(pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    commandList.dispatch(DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), 1u);
    // The original terminal fold is the only successful history-advance endpoint. Opaque recovery does not advance it.
    if(transparentReady && m_rayTracingState.m_softShadowTemporalReady)
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = true;
    return true;
}

b8 ShadowPipelineSet::ensureSoftCombinedUpsamplePipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_combinedUpsample.m_pipeline)
        return true;
    if(resolve.m_combinedUpsampleFailed || !resolve.m_bindingLayout)
        return false;
    auto& device = m_graphics.getDevice();
    if(
        !m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::shadowResolveCombinedCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CombinedShadowUpsample", resolve.m_combinedUpsample.m_shader)
        || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, resolve.m_combinedUpsample.m_shader, resolve.m_bindingLayout, false, resolve.m_combinedUpsample.m_pipeline)
    ){
        resolve.m_combinedUpsampleFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SoftShadowCombinedWaveletInputs::valid()const noexcept{
    const Array<const Core::Texture*, 7> textures = { opaqueHistory, opaqueMoments, transparentHistory, transparentMoments, geometry, opaqueOutput, transparentOutput };
    return AllValidAndDistinct(Span<const Core::Texture* const>(textures.data(), textures.size()), [](const Core::Texture* texture)noexcept{ return texture != nullptr; }, [](const Core::Texture* current, const Core::Texture* previous)noexcept{ return current == previous; });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderSoftShadowCombinedWavelet(Core::CommandList& commandList, DeferredFrameTargets& targets){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    const b8 frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    if(
        !CanCombineSoftShadowWavelets(
            static_cast<b8>(resolve.m_combinedUpsample.m_pipeline),
            static_cast<b8>(resolve.m_combinedWavelet.m_pipeline),
            m_rayTracingState.m_softShadowTemporalReady,
            m_rayTracingState.m_softTransparentTemporalReady,
            NWB_SHADOW_RESOLVE_PASS_COUNT,
            NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT
        )
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || !targets.bindless.valid() || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;

    // This graph-only phase inherits both history/moments reads and both distinct output UAV states.
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
    commandList.commitBarriers();
    const DeferredBindlessFrameResources& bindless = targets.bindless;
    const ShadowCombinedWaveletPushConstants push = MakeShadowCombinedWaveletPushConstants(
        targets.width,
        targets.height,
        DivideUp(targets.width, static_cast<u32>(targets.shadowReceiverFactor)),
        DivideUp(targets.height, static_cast<u32>(targets.shadowReceiverFactor)),
        ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask),
        bindless.shadowSoftGeometry.slot(),
        frontIsA ? bindless.transparentHistB.slot() : bindless.transparentHistA.slot(),
        frontIsA ? bindless.transparentMomentsB.slot() : bindless.transparentMomentsA.slot(),
        bindless.shadowSoftHalfAStorage.slot(),
        frontIsA ? bindless.shadowHistB.slot() : bindless.shadowHistA.slot(),
        frontIsA ? bindless.shadowMomentsB.slot() : bindless.shadowMomentsA.slot(),
        bindless.shadowSoftHalfBStorage.slot(),
        targets.shadowReceiverFactor
    );
    Core::ComputeState state;
    state.setPipeline(resolve.m_combinedWavelet.m_pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resolve.m_combinedWavelet.m_pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    commandList.dispatch(DivideUp(push.resolve.halfWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), DivideUp(push.resolve.halfHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), 1u);
    return true;
}

b8 ShadowPipelineSet::ensureSoftCombinedWaveletPipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_combinedWavelet.m_pipeline)
        return true;
    if(resolve.m_combinedWaveletFailed)
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &ShadowSubsystem::shadowResolveWaveletCombinedCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CombinedShadowWavelet",
        sizeof(ShadowCombinedWaveletPushConstants),
        false,
        resolve.m_combinedWaveletBindingLayout,
        resolve.m_combinedWavelet.m_shader,
        resolve.m_combinedWavelet.m_pipeline
    )){
        resolve.m_combinedWaveletFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::setShadowQualitySettings(const ShadowQualitySettings& settings)noexcept{
    if(!ValidateShadowQualitySettings(settings))
        return false;
    if(SameShadowQualitySettings(m_shadowQualitySettings, settings))
        return true;
    ApplyShadowQualitySettingsChange(
        settings,
        m_shadowQualitySettings,
        m_temporalOneShadowSamplingLogged,
        m_rayTracingState.m_transparentShadowSamplingHistory,
        m_rayTracingState.m_softShadowTemporalSeeded,
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending
    );
    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    return true;
}

b8 ShadowPipelineSet::ensureSoftwareTransparentSamplingPipeline(){
    auto& sampling = m_shadow.m_softwareTransparentSampling;
    if(sampling.m_pipelines[0] && sampling.m_pipelines[1])
        return true;
    auto& device = m_graphics.getDevice();
    const Array<AStringView, 2u> variants = { s_SwTransparentAdaptiveDisabledVariant, s_SwTransparentAdaptiveEnabledVariant };
    for(u32 index = 0u; index < variants.size(); ++index){
        if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
            m_arena,
            device,
            m_shaderSystem,
            &ShadowSubsystem::swShadowTransparentSoftCs,
            variants[index],
            Name(s_SoftwareShadowShaderScope),
            sizeof(SoftwareTransparentSamplingPush),
            false,
            sampling.m_layout,
            sampling.m_shaders[index],
            sampling.m_pipelines[index]
        ))
            return false;
    }
    return true;
}

u32 RendererRayTracingSystem::transparentShadowSampleCount()const noexcept{
    const b8 historyUsable = m_rayTracingState.m_softTransparentTemporalReady && softShadowTemporalHistoryUsable() && m_rayTracingState.m_transparentShadowSamplingHistory.covers(m_rayTracingState.m_softShadowSlotMask);
    return ResolveTransparentShadowSampleCount(m_shadowQualitySettings, historyUsable);
}

void RendererRayTracingSystem::reportTransparentShadowSampling(const u32 sampleCount){
    LogTransparentShadowSamplingIfNeeded(sampleCount, hardwareTransparentShadowReady(), m_temporalOneShadowSamplingLogged);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(HardwareTransparentShadowPushConstants) == NWB_HW_TRANSPARENT_PUSH_CONSTANT_BYTES);
static_assert(offsetof(HardwareTransparentShadowPushConstants, receiverFactor) == s_HwTransparentReceiverFactorByteOffset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 HardwareTransparentShadowResources::prepareHardwareTransparentShadowResources(DeferredFrameTargets& targets){
    auto& state = m_state;
    state.m_ready = false;
    if(state.m_pipelineFailed)
        return false;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout())
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(HardwareTransparentShadowPushConstants), Core::ShaderType::Compute, state.m_bindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow binding layout"));
        state.m_pipelineFailed = true;
        return false;
    }
    const Array<const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::*, 3u> shaderMembers = {
        &ShadowSubsystem::hardwareTransparentGatherCs,
        &ShadowSubsystem::hardwareTransparentEvaluateCs,
        &ShadowSubsystem::hardwareTransparentOverflowCs,
    };
    for(u32 i = 0u; i < LengthOf(state.m_pipelines); ++i){
        if(state.m_pipelines[i])
            continue;
        if(!m_shaderSystem.loadShader<ComputeShader>(shaderMembers[i], Core::ShaderArchive::s_DefaultVariant, "ECSRender_HardwareTransparentShadow", state.m_shaders[i])){
            state.m_pipelineFailed = true;
            return false;
        }
        if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, state.m_shaders[i], state.m_bindingLayout, true, state.m_pipelines[i])){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow pipeline {}"), i);
            state.m_pipelineFailed = true;
            return false;
        }
    }

    u32 pixels = 0u;
    Array<u64, 3u> sizes{};
    if(!ValidateHardwareTransparentShadowScratchExtent(
        targets.width,
        targets.height,
        targets.shadowReceiverFactor,
        NWB_HW_TRANSPARENT_WORDS_PER_RAY,
        NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS,
        pixels,
        sizes
    ))
        return false;
    if(pixels > state.m_pixelsCapacity){
        const Array<Name, 3u> names = { Name(s_HardwareShadowCrossings), Name(s_HardwareShadowOverflowList), Name(s_HardwareShadowOverflowArgs), };
        Array<Core::BufferHandle, s_HwTransparentScratchBufferCount> buffers{};
        Array<Core::GpuDescriptorHandle, s_HwTransparentScratchBufferCount> descriptors{};
        Core::GpuDescriptorRetirementGuard retireUnpublished(heap, Span<const Core::GpuDescriptorHandle>(descriptors));
        for(u32 i = 0u; i < buffers.size(); ++i){
            Core::BufferDesc desc;
            desc
                .setByteSize(sizes[i])
                .setStructStride(sizeof(u32))
                .setCanHaveUAVs(true)
                .setIsDrawIndirectArgs(i == s_HwTransparentOverflowArgsBufferIndex)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setDebugName(names[i])
                .enableAutomaticStateTracking(Core::ResourceStates::UnorderedAccess)
            ;
            auto buffersResult = m_graphics.createBuffer(desc);
            if(buffersResult)
                buffers[i] = Move(*buffersResult);
            else
                buffers[i].reset();
            if(!buffersResult){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow scratch {}"), i);
                return false;
            }
            const auto descriptor = RayTracingDetail::RegisterHeapBuffer(heap, *buffers[i], Core::GpuDescriptorClass::StorageBuffer, true);
            if(!descriptor){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow scratch {}"), i);
                return false;
            }
            descriptors[i] = *descriptor;
        }
        RayTracingDetail::RetireHeapHandle(heap, state.m_crossingsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowListHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowArgsHeapHandle);
        state.m_crossingsBuffer = Move(buffers[s_HwTransparentCrossingsBufferIndex]);
        state.m_overflowListBuffer = Move(buffers[s_HwTransparentOverflowListBufferIndex]);
        state.m_overflowArgsBuffer = Move(buffers[s_HwTransparentOverflowArgsBufferIndex]);
        state.m_crossingsHeapHandle = descriptors[s_HwTransparentCrossingsBufferIndex];
        state.m_overflowListHeapHandle = descriptors[s_HwTransparentOverflowListBufferIndex];
        state.m_overflowArgsHeapHandle = descriptors[s_HwTransparentOverflowArgsBufferIndex];
        for(auto& handle : descriptors)
            handle = Core::GpuDescriptorHandle::Invalid();
        state.m_pixelsCapacity = static_cast<u32>(pixels);
    }
    state.m_ready = true;
    return true;
}

void HardwareTransparentShadowResources::releaseHardwareTransparentShadowResources(){
    auto& state = m_state;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    RayTracingDetail::RetireHeapHandle(heap, state.m_crossingsHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, state.m_overflowListHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, state.m_overflowArgsHeapHandle);
    state = HardwareTransparentShadowState{};
}

b8 RendererRayTracingSystem::hardwareTransparentShadowReady()const noexcept{
    return m_shadowVisibilityHardwareSupported && m_rayTracingState.m_hardwareTransparentShadow.m_ready;
}

void RendererRayTracingSystem::dispatchHardwareTransparentShadow(Core::CommandList& commandList, DeferredFrameTargets& targets, const u32 frameIndex){
    auto& state = m_rayTracingState.m_hardwareTransparentShadow;
    NWB_ASSERT(state.m_ready);
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const u32 halfWidth = DivideUp(targets.width, static_cast<u32>(targets.shadowReceiverFactor));
    const u32 halfHeight = DivideUp(targets.height, static_cast<u32>(targets.shadowReceiverFactor));
    const u32 groupsX = DivideUp(halfWidth, static_cast<u32>(NWB_HW_TRANSPARENT_GROUP_SIZE));
    const u32 groupsY = DivideUp(halfHeight, static_cast<u32>(NWB_HW_TRANSPARENT_GROUP_SIZE));
    const u32 sampleCount = transparentShadowSampleCount();
    HardwareTransparentShadowPushConstants push = ResolveHardwareTransparentShadowPushConstants(
        targets.width,
        targets.height,
        frameIndex,
        sampleCount,
        targets.bindless.slotsBufferDescriptor.slot(),
        m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        state.m_crossingsHeapHandle.slot(),
        state.m_overflowListHeapHandle.slot(),
        state.m_overflowArgsHeapHandle.slot(),
        targets.bindless.transparentSoftHalfStorage.slot(),
        targets.shadowReceiverFactor
    );
    for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
        if((m_rayTracingState.m_softShadowSlotMask & (1u << slot)) == 0u)
            continue;
        push.lightSlot = slot;
        for(u32 sample = 0u; sample < sampleCount; ++sample){
            push.sampleIndex = sample;
            const Array<u32, NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS> emptyArgs = { 0u, 1u, 1u, 0u };
            if(!commandList.tryWriteBuffer(*state.m_overflowArgsBuffer, emptyArgs.data(), sizeof(emptyArgs))){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to reset hardware transparent-shadow continuation arguments"));
                return;
            }
            commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::UnorderedAccess, true);
            commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::UnorderedAccess, true);
            commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            const auto bind = [&](const u32 stage, const b8 indirect){
                Core::ComputeState compute;
                compute.setPipeline(state.m_pipelines[stage].get());
                if(indirect)
                    compute.setIndirectParams(state.m_overflowArgsBuffer.get());
                commandList.setComputeState(compute);
                heap.bindCompute(commandList, *state.m_pipelines[stage], m_rayTracingState.m_tlasHeapHandle);
                commandList.setPushConstants(&push, sizeof(push));
            };
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentGather, device, commandList);

                bind(s_HwTransparentGatherStage, false);
                commandList.dispatch(groupsX, groupsY, 1u);
            }
            commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::IndirectArgument | Core::ResourceStates::ShaderResource);
            commandList.commitBarriers();
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentEvaluate, device, commandList);

                bind(s_HwTransparentEvaluateStage, false);
                commandList.dispatch(groupsX, groupsY, 1u);
            }
            // Fast evaluation and continuation write disjoint pixels, but the next sample reads their accumulated result.
            commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentContinuation, device, commandList);

                bind(s_HwTransparentOverflowStage, true);
                commandList.dispatchIndirect(0u);
            }
        }
    }
    if(m_rayTracingState.m_softShadowSlotMask != 0u)
        reportTransparentShadowSampling(sampleCount);
    // The trace task exports scratch in its declared UAV state, including the indirect-argument allocation.
    commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();
    LogHardwareTransparentShadowDispatchIfNeeded(
        targets.width,
        targets.height,
        m_rayTracingState.m_tlasInstanceCount,
        state.m_dispatchLogged
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowOccluderMaterialGather::resolve(const Core::ECS::EntityID entity, const Core::Assets::AssetRef<Material>& material, const Scene::TransformComponent* const transform, const u32 meshInstanceIndex, NwbRtInstanceMaterialGpu& outInstanceMaterial, InstanceGpuData& outShadowInstance, b8* const outSceneHasTransparentOccluder){
    outInstanceMaterial = {};
    outShadowInstance = {};
    MaterialSurfaceInfo* materialInfo = nullptr;
    if(const auto materialInfoResult = m_materialSystem.findMaterialSurfaceInfo(material); materialInfoResult){
        materialInfo = materialInfoResult->get();
        if(materialInfo->transparent && outSceneHasTransparentOccluder)
            *outSceneHasTransparentOccluder = true;
        // Trace dispatcher reads Texture2D fields via non-uniform slots. Retain resolved handles in preflight.
        const b8 sampledTexturesReady = materialInfo->surfaceDispatchId == Limit<u32>::s_Max || m_sampledTextureCollector.collect(
            *materialInfo,
            [this](const MaterialSurfaceInfo& surface, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& pending){
                return AppendPreparedMaterialSurfaceSampledTextures(surface, m_materialSystem.materialState().m_resourceState, pending);
            }
        );
        if(!sampledTexturesReady)
            return false;
        const auto materialContext = m_materialSystem.appendShadowOccluderMaterialContext(entity, *materialInfo, transform, m_shadowMaterialTypedBytes, m_shadowConstantTypedRanges, m_shadowMutableTypedRanges);
        if(!materialContext)
            return false;
        outShadowInstance = materialContext->instance;
        outInstanceMaterial = ResolveInstanceShadowMaterialData(
            materialInfo->surfaceDispatchId,
            materialInfo->shadingModelId,
            materialInfo->transparent,
            materialInfo->refractive,
            materialContext->constantByteOffset,
            meshInstanceIndex
        );
    }
    if(!materialInfo || materialInfo->surfaceDispatchId == Limit<u32>::s_Max)
        m_contentComplete = false;
    // An unavailable hook need not hide a boundary. Unknown/untransparent surfaces cannot use the outside shortcut.
    if(!materialInfo || (materialInfo->transparent && materialInfo->surfaceDispatchId == Limit<u32>::s_Max))
        m_opticalScene.markIncomplete();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(InstanceGpuData) == s_InstanceGpuDataByteSize, "Shadow instance payload size must match the mesh GPU layout");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PreparedShadowMaterialContext::StorageReady(const RendererRayTracingState& state, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    return
        state.m_shadowInstanceMaterialBuffer
        && state.m_shadowInstanceBuffer
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowInstanceMaterialCapacity >= instanceMaterialCount
        && state.m_shadowInstanceCapacity >= instanceCount
        && state.m_shadowMaterialTypedCapacity >= materialTypedByteCount
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceMaterialHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(state.m_shadowMaterialTypedHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ;
}


b8 PreparedShadowMaterialContext::capture(
    const RendererRayTracingState& state,
    const PreparedShadowMaterialContextRoute::Enum route,
    const b8 staticScene,
    const u64 hash,
    const Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
    const Span<const InstanceGpuData> instances,
    const Span<const u8> materialTypedBytes
){
    const usize instanceMaterialCount = instanceMaterials.size();
    const usize instanceCount = instances.size();
    const usize materialTypedByteCount = materialTypedBytes.size();
    const b8 validStorage = StorageReady(state, instanceMaterialCount, instanceCount, materialTypedByteCount);
    if(
        route == PreparedShadowMaterialContextRoute::None
        || instanceMaterialCount == 0u
        || instanceCount == 0u
        || materialTypedByteCount == 0u
        || !validStorage
    )
        return false;

    clear();
    ShadowMaterialContextPolicy().captureBytes(
        instanceMaterials,
        instances,
        materialTypedBytes,
        m_instanceMaterialBytes,
        m_instanceBytes,
        m_materialTypedBytes
    );
    captureStorage(state, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
    m_route = route;
    m_static = staticScene;
    m_ready = true;
    m_uploadRequired = true;
    return true;
}

b8 PreparedShadowMaterialContext::captureCacheReuse(const RendererRayTracingState& state, const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    clear();
    if(instanceMaterialCount == 0u || instanceCount == 0u || materialTypedByteCount == 0u)
        return false;

    const b8 validStorage = StorageReady(state, instanceMaterialCount, instanceCount, materialTypedByteCount);
    if(!state.m_swShadowMaterialContextHashValid || state.m_swShadowMaterialContextHash != hash || !validStorage)
        return false;

    captureStorage(state, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
    m_route = PreparedShadowMaterialContextRoute::Software;
    m_static = true;
    m_ready = true;
    m_uploadRequired = false;
    return true;
}

void PreparedShadowMaterialContext::clear()noexcept{
    m_instanceMaterialBytes.clear();
    m_instanceBytes.clear();
    m_materialTypedBytes.clear();
    m_instanceMaterialBuffer = nullptr;
    m_instanceBuffer = nullptr;
    m_materialTypedBuffer = nullptr;
    m_instanceMaterialHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_instanceHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_materialTypedHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_instanceMaterialCount = 0u;
    m_instanceCount = 0u;
    m_materialTypedUploadBytes = 0u;
    m_instanceMaterialCapacity = 0u;
    m_instanceCapacity = 0u;
    m_materialTypedCapacity = 0u;
    m_hash = 0u;
    m_route = PreparedShadowMaterialContextRoute::None;
    m_static = false;
    m_ready = false;
    m_uploadRequired = false;
}

Expected<PreparedShadowMaterialContextUploads> PreparedShadowMaterialContext::retainUploads(const RendererRayTracingState& state, Core::GpuTaskGraph& graph)const{
    PreparedShadowMaterialContextUploads result;
    if(!m_ready)
        return result;

    if(!ShadowMaterialContextPolicy().validateRetainState(
        m_route,
        m_instanceMaterialCount,
        m_instanceCount,
        m_materialTypedUploadBytes,
        storageMatches(state),
        state.m_shadowInstanceMaterialCapacity,
        m_instanceMaterialCapacity,
        state.m_shadowInstanceCapacity,
        m_instanceCapacity,
        state.m_shadowMaterialTypedCapacity,
        m_materialTypedCapacity,
        StorageReady(state, m_instanceMaterialCount, m_instanceCount, m_materialTypedUploadBytes),
        m_uploadRequired,
        m_static,
        state.m_swShadowMaterialContextHashValid,
        state.m_swShadowMaterialContextHash,
        m_hash,
        state.m_sceneBvhInstanceCount,
        m_instanceMaterialBytes.size(),
        m_instanceBytes.size(),
        m_materialTypedBytes.size()
    ))
        return MakeUnexpected(Failure{});
    if(!m_uploadRequired)
        return result;

    const auto instanceMaterialResult = graph.copyUploadData(m_instanceMaterialBytes.data(), m_instanceMaterialBytes.size(), alignof(NwbRtInstanceMaterialGpu));
    const auto instanceResult = graph.copyUploadData(m_instanceBytes.data(), m_instanceBytes.size(), alignof(InstanceGpuData));
    const auto materialTypedResult = graph.copyUploadData(m_materialTypedBytes.data(), m_materialTypedBytes.size(), alignof(u32));
    if(!(instanceMaterialResult && instanceResult && materialTypedResult))
        return MakeUnexpected(Failure{});
    result.instanceMaterial = *instanceMaterialResult;
    result.instance = *instanceResult;
    result.materialTyped = *materialTypedResult;
    return result;
}

b8 PreparedShadowMaterialContext::publishAcceptedUpload(RendererRayTracingState& state)const noexcept{
    return ShadowMaterialContextPolicy().publishAcceptedUpload(
        m_ready,
        m_uploadRequired,
        m_route,
        m_hash,
        m_static,
        state.m_hwShadowMaterialContextHash,
        state.m_hwShadowMaterialContextHashValid,
        state.m_swShadowMaterialContextHash,
        state.m_swShadowMaterialContextHashValid
    );
}

b8 PreparedShadowMaterialContext::storageMatches(const RendererRayTracingState& state)const noexcept{
    return
        state.m_shadowInstanceMaterialBuffer.get() == m_instanceMaterialBuffer.get()
        && state.m_shadowInstanceBuffer.get() == m_instanceBuffer.get()
        && state.m_shadowMaterialTypedBuffer.get() == m_materialTypedBuffer.get()
        && state.m_shadowInstanceMaterialHeapHandle == m_instanceMaterialHeapHandle
        && state.m_shadowInstanceHeapHandle == m_instanceHeapHandle
        && state.m_shadowMaterialTypedHeapHandle == m_materialTypedHeapHandle
    ;
}

b8 PreparedShadowMaterialContext::softwareSnapshotCovers(const usize sceneInstanceCount)const noexcept{
    return ShadowMaterialContextPolicy().softwareSnapshotCovers(m_ready, m_route, m_instanceMaterialCount, m_instanceCount, sceneInstanceCount);
}

b8 PreparedShadowMaterialContext::softwareSnapshotAccepted(const RendererRayTracingState& state)const noexcept{
    return ShadowMaterialContextPolicy().softwareSnapshotAccepted(
        m_uploadRequired,
        m_static,
        state.m_swShadowMaterialContextHashValid,
        state.m_swShadowMaterialContextHash,
        m_hash,
        state.m_sceneBvhInstanceCount,
        m_instanceCount
    );
}

void PreparedShadowMaterialContext::captureStorage(const RendererRayTracingState& state, const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    m_instanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer;
    m_instanceBuffer = state.m_shadowInstanceBuffer;
    m_materialTypedBuffer = state.m_shadowMaterialTypedBuffer;
    m_instanceMaterialHeapHandle = state.m_shadowInstanceMaterialHeapHandle;
    m_instanceHeapHandle = state.m_shadowInstanceHeapHandle;
    m_materialTypedHeapHandle = state.m_shadowMaterialTypedHeapHandle;
    m_instanceMaterialCount = instanceMaterialCount;
    m_instanceCount = instanceCount;
    m_materialTypedUploadBytes = materialTypedByteCount;
    m_instanceMaterialCapacity = state.m_shadowInstanceMaterialCapacity;
    m_instanceCapacity = state.m_shadowInstanceCapacity;
    m_materialTypedCapacity = state.m_shadowMaterialTypedCapacity;
    m_hash = hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MergePreparedShadowMaterialSampledTextures(const Span<const Core::TextureHandle> sampledTextures, MaterialSampledTextureCollector<Core::Alloc::GlobalArena>& collector){
    for(const Core::TextureHandle& texture : sampledTextures){
        if(!texture || !texture->getCreationDescription().name)
            return false;
        collector.append(texture);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
