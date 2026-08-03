// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/assets/graphics/shadow/shadow_reproject_merge_binding_slots.h>
#include <impl/assets/graphics/shadow/names.h>
#include <impl/assets/graphics/caustic/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/hw_binding_slots.h>
#include <impl/assets/graphics/caustic/photon_push_constants.h>
#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/assets/graphics/caustic/names.h>
#include <impl/assets/graphics/raytrace/constants.h>
#include <impl/assets/graphics/bvh/binding_slots.h>
#include <impl/assets/graphics/bvh/constants.h>
#include <impl/assets/graphics/bvh/names.h>
#include <impl/assets/graphics/gi/names.h>
#include <impl/assets/graphics/gi/sw_binding_slots.h>
#include <impl/assets/graphics/gi/surfel/surfel_binding_slots.h>
#include <impl/assets/graphics/shadow/constants.h>
#include <global/environment.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Finite infinity for CPU BVH bounds.
inline constexpr f32 s_RayTracingFiniteInfinity = 1e30f;
inline constexpr u32 s_RayTracingTriangleIndexCount = NWB_RAYTRACE_TRIANGLE_CORNER_COUNT;
inline constexpr u32 s_RayTracingAllInstanceMask = NWB_RAY_TRACING_ALL_INSTANCE_MASK;
// Heap geometry buffers per mesh.
inline constexpr u32 s_HardwareRayTracingMeshBufferCount = 3u;
inline constexpr u32 s_SoftwareRayTracingMeshBufferCount = 4u;


// Limit skinned BLAS refits before rebuilding topology.
inline constexpr u32 s_BlasMaxRefitsBeforeRebuild = 8u;

// Scale rebuild cadence by mesh size, clamped to preserve quality.
inline constexpr u32 s_BlasMinRefitsBeforeRebuild = 4u;
inline constexpr u32 s_BlasMaxRefitsBeforeRebuildCap = 64u;
inline constexpr u32 s_BlasRefitBudgetLog2ExponentStep = 3u;

// Cube-root-scaled refit budget.
[[nodiscard]] inline constexpr u32 adaptiveRefitsBeforeRebuild(const u32 primitiveCount)noexcept{
    u32 log2 = 0u;
    u32 n = primitiveCount;
    while(n > 1u){ n >>= 1u; ++log2; }
    u32 budget = s_BlasMaxRefitsBeforeRebuild << (log2 / s_BlasRefitBudgetLog2ExponentStep);
    if(budget < s_BlasMinRefitsBeforeRebuild)
        budget = s_BlasMinRefitsBeforeRebuild;
    if(budget > s_BlasMaxRefitsBeforeRebuildCap)
        budget = s_BlasMaxRefitsBeforeRebuildCap;
    return budget;
}

inline constexpr usize s_TlasInitialInstanceCapacity = 128u;

inline constexpr usize s_BvhSortInitialCapacity = 1024u;

// Async-safe transparent-shadow edge-stat readback cadence.
inline constexpr u32 s_SwShadowEdgeStatsPeriod = 120u;
inline constexpr u32 s_SwShadowEdgeStatsLogDelay = 8u;

// CPU mirror of NwbBvhBitonicSortPushConstants.
struct BvhSortPushConstants{
    u32 elementCount = 0u;
    u32 compareDistance = 0u;
    u32 sequenceSize = 0u;
    u32 mode = 0u;
    u32 keysHeapSlot = Limit<u32>::s_Max;
    u32 payloadHeapSlot = Limit<u32>::s_Max;
    u32 pad[2] = {};
};
static_assert(sizeof(BvhSortPushConstants) == sizeof(u32) * 8u, "BvhSortPushConstants must match the shader NwbBvhBitonicSortPushConstants layout");

inline constexpr usize s_BvhBuildInitialCapacity = 1024u;

// Maximum per-mesh software BVH size; oversized meshes are skipped.
inline constexpr u32 s_BvhMaxPrimitivesPerMesh = 262144u;

// CPU mirror of the shader BVH node.
struct NwbBvhNodeGpu{
    Float3UInt aabbMinLeftChild;
    Float3UInt aabbMaxRightChild;
};
static_assert(sizeof(NwbBvhNodeGpu) == 32u, "NwbBvhNodeGpu must match the shader NwbBvhNode std430 layout");

// Scratch scene-BVH build values.
struct SceneBvhPrimitiveCalculation{
    SIMDVector aabbMin = {};
    SIMDVector aabbMax = {};
    SIMDVector centroid = {};
    bool transparentOccluder = false;
};

struct SceneBvhNodeCalculation{
    SIMDVector aabbMin = {};
    SIMDVector aabbMax = {};
    u32 leftChild = 0u;
    u32 rightChild = 0u;
    bool containsTransparentOccluder = false;
};

// CPU mirror of NwbBvhBuildPushConstants.
struct BvhBuildPushConstants{
    u32 primitiveCount = 0u;
    u32 internalCount = 0u;
    u32 refitMode = 0u;
    u32 pad0 = 0u;
    u32 positionHeapSlot = Limit<u32>::s_Max;
    u32 triangleIndexHeapSlot = Limit<u32>::s_Max;
    u32 keysHeapSlot = Limit<u32>::s_Max;
    u32 payloadHeapSlot = Limit<u32>::s_Max;
    u32 nodeHeapSlot = Limit<u32>::s_Max;
    u32 parentHeapSlot = Limit<u32>::s_Max;
    u32 visitCounterHeapSlot = Limit<u32>::s_Max;
    u32 pad1 = 0u;
    Float4 aabbMin = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 aabbMax = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(sizeof(BvhBuildPushConstants) == sizeof(u32) * 12u + sizeof(Float4) * 2u, "BvhBuildPushConstants must match the shader NwbBvhBuildPushConstants layout");

// Shader-mirrored BVH child encoding.
namespace BvhNodeIndex{
    enum Mask : u32{
        LeafFlag = NWB_BVH_LEAF_FLAG,
        // Reuses LeafFlag in rightChild to mark transparent subtrees.
        TransparentSubtreeFlag = NWB_BVH_TRANSPARENT_SUBTREE_FLAG,
        ChildIndexMask = NWB_BVH_CHILD_INDEX_MASK,
        Invalid = NWB_BVH_INVALID,
    };
};

inline constexpr usize s_SceneBvhInitialInstanceCapacity = 64u;

// Fixed-grid binned SAH; leaf cost weights instance intersection cost.
inline constexpr u32 s_SceneBvhAxisCount = 3u;
inline constexpr u32 s_SceneBvhSahBinCount = 12u;
inline constexpr f32 s_SceneBvhSahTraversalCost = 1.0f;

// CPU mirror of the software scene-BVH instance ABI.
struct SceneSwBvhInstanceGpu{
    Float34 worldToObject{};
    u32 reservedMeshIndex = 0u;
    u32 primitiveCount = 0u;
    u32 pad0 = 0u;
    u32 pad1 = 0u;
};
static_assert(sizeof(SceneSwBvhInstanceGpu) == sizeof(Float34) + sizeof(u32) * 4u, "SceneSwBvhInstanceGpu must stay a tight 64-byte record");

inline constexpr usize s_CausticEmissionTargetInitialCapacity = 32u;

// CPU/GPU refractive-instance AABB record.
struct NwbCausticEmissionTargetGpu{
    Float4 aabbMin = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 aabbMax = Float4(0.f, 0.f, 0.f, 0.f);
};
static_assert(sizeof(NwbCausticEmissionTargetGpu) == sizeof(Float4) * 2u, "NwbCausticEmissionTargetGpu must stay a tight 32-byte std430 record");

// CPU mirror of trace material ABI; HW and SW share instance IDs.
struct NwbRtInstanceMaterialGpu{
    u32 shadowTransmittanceModelId = Limit<u32>::s_Max;
    u32 flags = 0u;
    u32 reservedMeshSlot = 0u;
    u32 materialConstantByteOffset = 0u;
    u32 meshInstanceIndex = 0u;
    // Global-heap geometry slots; nodeSlot is software-only.
    u32 indexSlot = Limit<u32>::s_Max;
    u32 attributeSlot = Limit<u32>::s_Max;
    u32 positionSlot = Limit<u32>::s_Max;
    u32 nodeSlot = Limit<u32>::s_Max;
};
static_assert(sizeof(NwbRtInstanceMaterialGpu) == 36u, "NwbRtInstanceMaterialGpu must match the shader NwbRtInstanceMaterial std430 layout (9 x uint)");

// Shader-mirrored flags: transparent selects transmittance; refractive selects caustics.
namespace RtInstanceMaterialFlag{
    enum Mask : u32{
        None = 0u,
        Transparent = NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT,
        Refractive = NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE,
    };
};

inline constexpr usize s_ShadowInstanceMaterialInitialCapacity = 128u;

// CPU mirror of the heap-only software-shadow selector block.
struct SwShadowHeapPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 frameIndex = 0u;
    u32 coarseWidth = 0u;
    u32 coarseHeight = 0u;
    f32 edgeThreshold = ECSRenderDetail::s_DefaultSwShadowEdgeThreshold;
    u32 collectStats = 0u;
    u32 edgeCapacity = 0u;
    u32 traceGroupSize = 0u;
    u32 deferredResourcesHeapSlot = 0u;
    u32 materialContextSlotsHeapSlot = 0u;
    u32 visibilityStorageSlot = 0u;
    u32 coarseStorageSlot = 0u;
    u32 softHalfStorageSlot = 0u;
    u32 transparentSoftHalfStorageSlot = 0u;
    u32 edgeStatsStorageSlot = 0u;
    u32 edgeCounterStorageSlot = 0u;
    u32 edgeListStorageSlot = 0u;
    u32 indirectArgsStorageSlot = 0u;
    u32 softSampleCount = NWB_SW_SHADOW_SOFT_SPP;
};
static_assert(NWB_SW_SHADOW_SOFT_TEMPORAL_SPP >= 1u && NWB_SW_SHADOW_SOFT_TEMPORAL_SPP <= NWB_SW_SHADOW_SOFT_SPP, "temporal soft-shadow sample budget must be within the bootstrap budget");
static_assert(sizeof(SwShadowHeapPushConstants) == sizeof(u32) * 21u, "SwShadowHeapPushConstants must match the shader push-constant layout");

// CPU mirror of hardware RayQuery shadow push constants.
struct ShadowRqPushConstants{
    u32 frameIndex = 0u;
    u32 worldPositionSlot = 0u;
    u32 normalSlot = 0u;
    u32 depthSlot = 0u;
    u32 deferredResourcesHeapSlot = 0u;
    u32 visibilityStorageSlot = 0u;
};
static_assert(sizeof(ShadowRqPushConstants) == sizeof(u32) * 6u, "ShadowRqPushConstants must match the shader push-constant layout");

// CPU mirror of half-resolution hardware soft-shadow push constants.
struct ShadowRqSoftPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 frameIndex = 0u;
    u32 worldPositionSlot = 0u;
    u32 normalSlot = 0u;
    u32 depthSlot = 0u;
    u32 deferredResourcesHeapSlot = 0u;
    u32 visibilityStorageSlot = 0u;
    u32 softSampleCount = NWB_SW_SHADOW_SOFT_SPP;
};
static_assert(sizeof(ShadowRqSoftPushConstants) == sizeof(u32) * 9u, "ShadowRqSoftPushConstants must match the shader push-constant layout");

// CPU mirror of NwbShadowResolvePushConstants.
struct ShadowResolvePushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;
    u32 stepWidth = 1u;
    u32 stage = NWB_SHADOW_RESOLVE_STAGE_PREPARE;
    u32 lightSlotStart = 0u;
    u32 lightSlotCount = 0u;
    u32 momentsValid = 0u;
    u32 upsampleFold = 0u;
    u32 geometrySlot = 0u;
    u32 depthSlot = 0u;
    u32 worldPositionSlot = 0u;
    u32 normalSlot = 0u;
    u32 softHalfSlot = 0u;
    u32 inputColorSlot = 0u;
    u32 momentsSlot = 0u;
    u32 outputStorageSlot = 0u;
    u32 visibilityStorageSlot = 0u;
    u32 sceneShadingSlot = 0u;
    u32 pad0 = 0u;
};
static_assert(sizeof(ShadowResolvePushConstants) == sizeof(u32) * 21u, "ShadowResolvePushConstants must match the shader push-constant layout");

// Shader-mirrored shadow resolve stages.
namespace ShadowResolveStage{
    enum Enum : u32{
        Prepare = NWB_SHADOW_RESOLVE_STAGE_PREPARE,
        Wavelet = NWB_SHADOW_RESOLVE_STAGE_WAVELET,
        Upsample = NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE,
    };
};

// CPU mirror of shadow geometry-downsample push constants.
struct ShadowGeometryDownsamplePushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;
    u32 worldPositionSlot = 0u;
    u32 normalSlot = 0u;
    u32 depthSlot = 0u;
    u32 outputStorageSlot = 0u;
    u32 sceneShadingSlot = 0u;
    u32 pad0 = 0u;
};
static_assert(sizeof(ShadowGeometryDownsamplePushConstants) == sizeof(u32) * 10u, "ShadowGeometryDownsamplePushConstants must match the shader push-constant layout");

// CPU mirror of temporal shadow-reprojection push constants.
struct ShadowReprojectMergePushConstants{
    Float44U prevWorldToClip = {};
    u32 width = 0u;
    u32 height = 0u;
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;
    u32 lightSlotStart = 0u;
    u32 lightSlotCount = 0u;
    u32 historyValid = 0u;
    u32 softTraceSlot = 0u;
    u32 historyInSlot = 0u;
    u32 momentsInSlot = 0u;
    u32 geometryCurrSlot = 0u;
    u32 geometryPrevSlot = 0u;
    u32 worldPositionSlot = 0u;
    u32 historyOutputStorageSlot = 0u;
    u32 momentsOutputStorageSlot = 0u;
    u32 pad2 = 0u;
};
static_assert(sizeof(ShadowReprojectMergePushConstants) == sizeof(f32) * 16u + sizeof(u32) * 16u, "ShadowReprojectMergePushConstants must match the shader push-constant layout");

// Shared CPU mirror of SW/HW caustic photon push constants.
struct CausticPhotonPushConstants{
#define NWB_CAUSTIC_PHOTON_PUSH_CONSTANT_FIELD(name, defaultValue) u32 name = defaultValue;
    NWB_CAUSTIC_PHOTON_PUSH_CONSTANTS_FIELDS(NWB_CAUSTIC_PHOTON_PUSH_CONSTANT_FIELD)
#undef NWB_CAUSTIC_PHOTON_PUSH_CONSTANT_FIELD
};
static_assert(sizeof(CausticPhotonPushConstants) == sizeof(u32) * 15u, "CausticPhotonPushConstants must match the shader push-constant layout");

// CPU mirror of caustic resolve push constants.
struct CausticResolvePushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;
    f32 causticIntensity = 0.f;
    u32 stepWidth = 1u;
    u32 stage = NWB_CAUSTIC_RESOLVE_STAGE_PREPARE_DOWNSAMPLE;
    u32 pad = 0u;
    u32 worldPositionSlot = 0u;
    u32 depthSlot = 0u;
    u32 inputColorSlot = 0u;
    u32 geometrySlot = 0u;
    u32 accumulatorSlot = 0u;
    u32 outputStorageSlot = 0u;
};
static_assert(sizeof(CausticResolvePushConstants) == sizeof(u32) * 13u + sizeof(f32), "CausticResolvePushConstants must match the shader push-constant layout");

// Heap-only selector ABI shared by every surfel GI pass.
struct SurfelHeapPushConstants{
    u32 constantsHeapSlot = 0u;
    u32 poolHeapSlot = 0u;
    u32 cellHeadHeapSlot = 0u;
    u32 counterHeapSlot = 0u;
    u32 freeListHeapSlot = 0u;
    u32 snapshotPoolHeapSlot = 0u;
    u32 snapshotCellHeadHeapSlot = 0u;
    u32 traceIndirectArgsHeapSlot = 0u;
    u32 deferredResourcesHeapSlot = 0u;
    u32 materialContextSlotsHeapSlot = 0u;
    u32 worldPositionSlot = 0u;
    u32 normalSlot = 0u;
    u32 outputStorageHeapSlot = 0u;
    u32 halfIrradianceSlot = 0u;
};
static_assert(sizeof(SurfelHeapPushConstants) == sizeof(u32) * 14u, "SurfelHeapPushConstants must match NwbSurfelHeapPushConstants");
static_assert(NWB_SURFEL_CONVERGED_RAYS_PER_SURFEL >= 1u && NWB_SURFEL_CONVERGED_RAYS_PER_SURFEL <= NWB_SURFEL_RAYS_PER_SURFEL);
static_assert(NWB_SURFEL_CONVERGED_SAMPLE_COUNT >= 1u && NWB_SURFEL_CONVERGED_SAMPLE_COUNT <= NWB_SURFEL_MAX_ACCUM);

// Shader-mirrored caustic resolve stages.
namespace CausticResolveStage{
    enum Enum : u32{
        PrepareDownsample = NWB_CAUSTIC_RESOLVE_STAGE_PREPARE_DOWNSAMPLE,
        Wavelet = NWB_CAUSTIC_RESOLVE_STAGE_WAVELET,
        Upsample = NWB_CAUSTIC_RESOLVE_STAGE_UPSAMPLE,
    };
};

// CPU mirror of caustic geometry-downsample push constants.
struct CausticGeometryDownsamplePushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;
    u32 worldPositionSlot = 0u;
    u32 depthSlot = 0u;
    u32 outputStorageSlot = 0u;
    u32 pad1 = 0u;
};
static_assert(sizeof(CausticGeometryDownsamplePushConstants) == sizeof(u32) * 8u, "CausticGeometryDownsamplePushConstants must match the shader push-constant layout");

// CPU mirror of caustic accumulator-decay push constants.
struct CausticAccumulatorDecayPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    f32 decayFactor = 0.f;
    u32 accumulatorStorageSlot = 0u;
};
static_assert(sizeof(CausticAccumulatorDecayPushConstants) == sizeof(u32) * 4u, "CausticAccumulatorDecayPushConstants must match the shader push-constant layout");

// Hardware uses full density; photon count must equal gridSide squared.
inline constexpr u32 s_CausticHwPhotonGridSide = 512u;
inline constexpr u32 s_CausticHwPhotonCount = s_CausticHwPhotonGridSide * s_CausticHwPhotonGridSide;
#if defined(NDEBUG)
inline constexpr u32 s_CausticSwPhotonGridSide = 512u;
#else
inline constexpr u32 s_CausticSwPhotonGridSide = NWB_CAUSTIC_SW_GRID_SIDE;
#endif
inline constexpr u32 s_CausticSwPhotonCount = s_CausticSwPhotonGridSide * s_CausticSwPhotonGridSide;

// Two bootstrap phases, then four converged phases with normalized flux.
inline constexpr u32 s_CausticTemporalBootstrapPhaseCount = NWB_CAUSTIC_TEMPORAL_BOOTSTRAP_PHASE_COUNT;
inline constexpr u32 s_CausticTemporalConvergedPhaseCount = NWB_CAUSTIC_TEMPORAL_CONVERGED_PHASE_COUNT;
inline constexpr u32 s_CausticTemporalWarmupFrameCount = NWB_CAUSTIC_TEMPORAL_WARMUP_FRAME_COUNT;
static_assert(s_CausticTemporalBootstrapPhaseCount > NWB_CAUSTIC_TEMPORAL_DISABLED_PHASE_COUNT);
static_assert(s_CausticTemporalConvergedPhaseCount > s_CausticTemporalBootstrapPhaseCount);
static_assert((s_CausticHwPhotonGridSide % s_CausticTemporalConvergedPhaseCount) == 0u);
static_assert((s_CausticSwPhotonGridSide % s_CausticTemporalConvergedPhaseCount) == 0u);

// Resolve exposure; photon-count changes preserve brightness.
inline constexpr f32 s_CausticIntensity = 2.0f;

// Runtime refractor bounds are inflated because emission uses bind-pose AABBs.
inline constexpr f32 s_CausticRuntimeBoundsInflation = 1.25f;

// Conservative top-level bounds for transparent-shadow rays.
inline constexpr f32 s_SwShadowSceneBoundsMinPadding = 0.25f;
inline constexpr f32 s_SwShadowSceneBoundsRelativePadding = 0.10f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cross-TU ray-tracing helpers.
namespace __hidden_raytracing_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InflateSwShadowSceneBounds(SIMDVector& boundsMin, SIMDVector& boundsMax)noexcept;
[[nodiscard]] bool ResolveRenderableMeshResources(
    MeshSystem& meshSystem,
    RendererMeshSystem& rendererMeshSystem,
    const Core::ECS::EntityID entity,
    RenderableMeshDesc& outResolvedMesh,
    MeshResources*& outMesh
);
[[nodiscard]] bool IsHeapHandle(const Core::GpuDescriptorHandle handle, const Core::GpuDescriptorClass::Enum descriptorClass);
[[nodiscard]] bool RegisterHeapBuffer(
    Core::GpuDescriptorHeap& heap,
    Core::Buffer& buffer,
    Core::GpuDescriptorClass::Enum descriptorClass,
    bool writable,
    Core::GpuDescriptorHandle& outHandle
);
[[nodiscard]] bool EnsureHeapBuffer(
    Core::GpuDescriptorHeap& heap,
    Core::Buffer& buffer,
    Core::GpuDescriptorClass::Enum descriptorClass,
    bool writable,
    Core::GpuDescriptorHandle& inOutHandle
);
[[nodiscard]] bool ReplaceHeapBuffer(
    Core::GpuDescriptorHeap& heap,
    Core::Buffer& buffer,
    Core::GpuDescriptorClass::Enum descriptorClass,
    bool writable,
    Core::GpuDescriptorHandle& inOutHandle
);
void RetireHeapHandle(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle& handle);
u32 BuildSceneBvhNode(
    u32* indices,
    const u32 lo,
    const u32 hi,
    const SceneBvhPrimitiveCalculation* primitiveBounds,
    Vector<SceneBvhNodeCalculation, Core::Alloc::ScratchArena>& nodes,
    const u32* instanceLeafCost = nullptr
);
[[nodiscard]] NwbRtInstanceMaterialGpu ResolveInstanceShadowMaterial(
    const MaterialSurfaceInfo& materialInfo,
    const u32 materialConstantByteOffset,
    const u32 meshInstanceIndex
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

