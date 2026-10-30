// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "instance_material.h"
#include "caustic_photon_parameters.h"

#include <impl/ecs_render/raytrace/types.adhd.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/avboit/avboit.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/shared/renderer_scene_private.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/assets/graphics/shadow/shadow_reproject_merge_binding_slots.h>
#include <impl/assets/graphics/caustic/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/hw_binding_slots.h>
#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/assets/graphics/raytrace/constants.h>
#include <impl/assets/graphics/bvh/binding_slots.h>
#include <impl/assets/graphics/bvh/constants.h>
#include <impl/assets/graphics/gi/surfel/surfel_binding_slots.h>
#include <impl/assets_graphics/bvh/subsystem.adhd.h>
#include <impl/assets_graphics/gi/subsystem.adhd.h>
#include <impl/assets_graphics/refractive/subsystem.adhd.h>
#include <impl/assets_graphics/shadow/subsystem.adhd.h>
#include <impl/assets_graphics/transparent/subsystem.adhd.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets/graphics/avboit/binding_slots.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/assets/graphics/csg/constants.h>
#include <impl/assets/graphics/deferred/binding_slots.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_shader/loader.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>

#include <global/compile.h>
#include <global/algorithm.h>
#include <global/not_null.h>
#include <global/span.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shader variant selectors for the NWB_RT_CSG_ENABLED define.
inline constexpr AStringView s_RayTracingCsgDisabledVariant = "NWB_RT_CSG_ENABLED=0";
inline constexpr AStringView s_RayTracingCsgEnabledVariant = "NWB_RT_CSG_ENABLED=1";

static_assert(sizeof(BvhSortPushConstants) == sizeof(u32) * NWB_BVH_SORT_PUSH_CONSTANT_WORD_COUNT, "BvhSortPushConstants must match the shader NwbBvhBitonicSortPushConstants layout");
static_assert(offsetof(BvhSortPushConstants, mode) == 12u);
static_assert(offsetof(BvhSortPushConstants, keysHeapSlot) == 16u);
static_assert(offsetof(BvhSortPushConstants, payloadHeapSlot) == 20u);
static_assert(IsStandardLayout_V<BvhSortPushConstants>);
static_assert(IsTriviallyCopyable_V<BvhSortPushConstants>);

// CPU mirror of NwbBvhBuildPushConstants.
struct alignas(16) BvhBuildPushConstants{
    u32 primitiveCount = 0u;
    u32 internalCount = 0u;
    u32 refitMode = NWB_BVH_BUILD_MODE_FULL;
    u32 positionHeapSlot = Limit<u32>::s_Max;
    u32 triangleIndexHeapSlot = Limit<u32>::s_Max;
    u32 keysHeapSlot = Limit<u32>::s_Max;
    u32 payloadHeapSlot = Limit<u32>::s_Max;
    u32 nodeHeapSlot = Limit<u32>::s_Max;
    Float3U aabbMin = Float3U(0.0f, 0.0f, 0.0f);
    u32 parentHeapSlot = Limit<u32>::s_Max;
    Float3U aabbMax = Float3U(0.0f, 0.0f, 0.0f);
    u32 visitCounterHeapSlot = Limit<u32>::s_Max;
};
static_assert(sizeof(BvhBuildPushConstants) == 64u, "BvhBuildPushConstants must match the shader NwbBvhBuildPushConstants layout");
static_assert(alignof(BvhBuildPushConstants) == 16u);
static_assert(offsetof(BvhBuildPushConstants, aabbMin) == 32u);
static_assert(offsetof(BvhBuildPushConstants, parentHeapSlot) == 44u);
static_assert(offsetof(BvhBuildPushConstants, aabbMax) == 48u);
static_assert(offsetof(BvhBuildPushConstants, visitCounterHeapSlot) == 60u);
static_assert(IsStandardLayout_V<BvhBuildPushConstants>);
static_assert(IsTriviallyCopyable_V<BvhBuildPushConstants>);

static_assert(sizeof(NwbCausticEmissionTargetGpu) == sizeof(Float4) * 2u, "NwbCausticEmissionTargetGpu must stay a tight 32-byte std430 record");

static_assert(NWB_SW_SHADOW_SOFT_TEMPORAL_SPP >= 1u && NWB_SW_SHADOW_SOFT_TEMPORAL_SPP <= NWB_SW_SHADOW_SOFT_SPP, "temporal soft-shadow sample budget must be within the bootstrap budget");
static_assert(sizeof(SwShadowHeapPushConstants) == sizeof(u32) * 20u, "SwShadowHeapPushConstants must match the shader push-constant layout");
static_assert(offsetof(SwShadowHeapPushConstants, receiverFactor) == 76u);

static_assert(sizeof(ShadowRqPushConstants) == sizeof(u32) * 6u, "ShadowRqPushConstants must match the shader push-constant layout");

static_assert(sizeof(ShadowRqSoftPushConstants) == sizeof(u32) * 10u, "ShadowRqSoftPushConstants must match the shader push-constant layout");
static_assert(offsetof(ShadowRqSoftPushConstants, receiverFactor) == 36u);

static_assert(sizeof(ShadowResolvePushConstants) == sizeof(u32) * 22u, "ShadowResolvePushConstants must match the shader push-constant layout");
static_assert(offsetof(ShadowResolvePushConstants, receiverFactor) == 84u);
static_assert(offsetof(ShadowResolvePushConstants, opaqueInputColorSlot) == sizeof(u32) * 20u);

static_assert(sizeof(ShadowCombinedWaveletPushConstants) == 100u);
static_assert(offsetof(ShadowCombinedWaveletPushConstants, opaqueMomentsSlot) == s_ShadowCombinedWaveletPushConstantsOpaqueMomentsSlotOffset);
static_assert(offsetof(ShadowCombinedWaveletPushConstants, opaqueMomentsValid) == s_ShadowCombinedWaveletPushConstantsOpaqueMomentsValidOffset);
static_assert(offsetof(ShadowCombinedWaveletPushConstants, opaqueOutputStorageSlot) == s_ShadowCombinedWaveletPushConstantsOpaqueOutputStorageSlotOffset);

static_assert(sizeof(ShadowGeometryDownsamplePushConstants) == sizeof(u32) * 10u, "ShadowGeometryDownsamplePushConstants must match the shader push-constant layout");
static_assert(offsetof(ShadowGeometryDownsamplePushConstants, receiverFactor) == 36u);

static_assert(sizeof(ShadowReprojectMergePushConstants) == sizeof(f32) * 16u + sizeof(u32) * 16u, "ShadowReprojectMergePushConstants must match the shader push-constant layout");
static_assert(offsetof(ShadowReprojectMergePushConstants, receiverFactor) == 124u);

static_assert(sizeof(ShadowCombinedTemporalPushConstants) == 128u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, receiverFactor) == 64u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, opaqueSoftTraceSlot) == 88u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, transparentSoftTraceSlot) == 108u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, transparentMomentsOutSlot) == 124u);

static_assert(sizeof(CausticResolvePushConstants) == s_CausticResolvePushConstantsByteSize, "CausticResolvePushConstants must match the shader push-constant layout");
static_assert(offsetof(CausticResolvePushConstants, activityInputSlot) == s_CausticResolvePushConstantsActivityInputSlotOffset);
static_assert(offsetof(CausticResolvePushConstants, activityOutputSlot) == s_CausticResolvePushConstantsActivityOutputSlotOffset);

static_assert(sizeof(SurfelHeapPushConstants) == sizeof(u32) * 15u, "SurfelHeapPushConstants must match NwbSurfelHeapPushConstants");
static_assert(NWB_SURFEL_CONVERGED_RAYS_PER_SURFEL >= 1u && NWB_SURFEL_CONVERGED_RAYS_PER_SURFEL <= NWB_SURFEL_RAYS_PER_SURFEL);
static_assert(NWB_SURFEL_CONVERGED_UNIFORM_RAYS_PER_SURFEL >= 1u && NWB_SURFEL_CONVERGED_UNIFORM_RAYS_PER_SURFEL <= NWB_SURFEL_CONVERGED_RAYS_PER_SURFEL);
static_assert(NWB_SURFEL_CONVERGED_SAMPLE_COUNT >= 1u && NWB_SURFEL_CONVERGED_SAMPLE_COUNT <= NWB_SURFEL_MAX_ACCUM);
static_assert(NWB_SURFEL_GUIDE_BIN_COUNT == 16u && NWB_SURFEL_RECORD_SIZE == 96u);
static_assert(sizeof(Float4) == NWB_SURFEL_GUIDE_FLOAT4_STRIDE);
static_assert(NWB_SURFEL_RAYS_PER_SURFEL >= NWB_SURFEL_GUIDE_BIN_COUNT);

static_assert(sizeof(CausticGeometryDownsamplePushConstants) == sizeof(u32) * 7u, "CausticGeometryDownsamplePushConstants must match the shader push-constant layout");
static_assert(sizeof(CausticAccumulatorDecayPushConstants) == sizeof(u32) * 4u, "CausticAccumulatorDecayPushConstants must match the shader push-constant layout");

// Base grids retain full quality; the explicit quality divisor scales both dimensions before temporal phase selection.
#if defined(NWB_FINAL) || defined(NWB_OPTIMIZE)
inline constexpr u32 s_CausticSwPhotonGridSide = 512u;
#else
inline constexpr u32 s_CausticSwPhotonGridSide = NWB_CAUSTIC_SW_GRID_SIDE;
#endif

// Frame-parity reuse retains the same two-phase checkerboard for both photon producers.
static_assert(s_CausticTemporalPhaseCount == 2u);
static_assert(s_CausticTemporalPhaseCount > s_CausticTemporalDisabledPhaseCount);
static_assert((s_CausticHwPhotonGridSide % s_CausticTemporalPhaseCount) == 0u);
static_assert((s_CausticSwPhotonGridSide % s_CausticTemporalPhaseCount) == 0u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolve exposure on SIMD lanes; photon-count changes preserve brightness.
// Decay is intensity * (1 - decay); the temporal gate is a lane select, so both candidates stay on lanes and no scalar branch computes the survivor.
[[nodiscard]] NWB_INLINE SIMDVectorSingle EffectiveCausticIntensity(const SIMDVectorSingle decayLanes)noexcept{
    const SIMDVectorSingle intensityLanes = VectorReplicate(s_CausticIntensity);
    const SIMDVectorSingle decayedLanes = VectorNegativeMultiplySubtract(decayLanes, intensityLanes, intensityLanes);
    const SIMDVectorSingle gate = VectorGreater(decayLanes, VectorZero());
    return VectorSelect(intensityLanes, decayedLanes, gate);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cross-TU ray-tracing helpers.
namespace RayTracingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InflateSwShadowSceneBounds(SIMDVectorSingle& boundsMin, SIMDVectorSingle& boundsMax)noexcept;
struct RenderableRayTracingMeshResources{
    RenderableMeshDesc resolvedMesh;
    ECSRenderDetail::MeshRayTracingResourceSnapshot meshResources;
};

[[nodiscard]] Expected<RenderableRayTracingMeshResources, RenderableMeshResolution::Enum> ResolveRenderableMeshResources(MeshSystem& meshSystem, RendererMeshSystem& rendererMeshSystem, Core::ECS::EntityID entity);
[[nodiscard]] b8 IsHeapHandle(const Core::GpuDescriptorHandle handle, const Core::GpuDescriptorClass::Enum descriptorClass)noexcept;
[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterHeapBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, Core::GpuDescriptorClass::Enum descriptorClass, b8 writable);
[[nodiscard]] b8 EnsureHeapBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, Core::GpuDescriptorClass::Enum descriptorClass, b8 writable, Core::GpuDescriptorHandle& inOutHandle);
[[nodiscard]] b8 ReplaceHeapBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, Core::GpuDescriptorClass::Enum descriptorClass, b8 writable, Core::GpuDescriptorHandle& inOutHandle);
void RetireHeapHandle(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle& handle);
u32 BuildSceneBvhNode(const u32 lo, const u32 hi, const Span<const SceneBvhPrimitiveCalculation> primitiveBounds, const Span<const u32> instanceLeafCost, const Span<u32> indices, Vector<SceneBvhNodeCalculation, Core::Alloc::ScratchArena>& nodes);
[[nodiscard]] NwbRtInstanceMaterialGpu ResolveInstanceShadowMaterial(const MaterialSurfaceInfo& materialInfo, const u32 materialConstantByteOffset, const u32 meshInstanceIndex)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

