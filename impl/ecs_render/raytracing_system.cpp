// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include "renderer_private.h"
#include "arena_names.h"

#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/assets/graphics/shadow/names.h>
#include <impl/assets/graphics/bvh/binding_slots.h>
#include <impl/assets/graphics/bvh/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Number of in-place refits performed on a skinned BLAS before forcing a full rebuild. A refit
// (in-place UPDATE) keeps the build-pose tree topology and only resizes the bounding boxes, so
// traversal quality drifts as the pose moves away from the last full build; the periodic rebuild
// restores it. This is the quality/cost knob for skinned ray-traced geometry.
inline constexpr u32 s_BlasMaxRefitsBeforeRebuild = 8u;

// Initial scene-TLAS instance capacity; grows by doubling when the live instance count exceeds it.
inline constexpr usize s_TlasInitialInstanceCapacity = 128u;

// Initial element capacity of the BVH Morton-sort scratch buffers (keys + payload); grows by doubling.
inline constexpr usize s_BvhSortInitialCapacity = 1024u;

// CPU mirror of the shader NwbBvhBitonicSortPushConstants block (one bitonic compare-exchange step).
struct BvhSortPushConstants{
    u32 elementCount = 0u;
    u32 compareDistance = 0u;
    u32 sequenceSize = 0u;
    u32 pad0 = 0u;
};
static_assert(sizeof(BvhSortPushConstants) == sizeof(u32) * 4u, "BvhSortPushConstants must match the shader NwbBvhBitonicSortPushConstants layout");

// Initial per-mesh triangle capacity for the shared LBVH visit-counter scratch; grows by doubling.
inline constexpr usize s_BvhBuildInitialCapacity = 1024u;

// Largest mesh (in triangles) the software BVH supports. The shared sort/counter scratch is sized once to
// this cap so that per-mesh build binding sets — which reference those shared buffers — never reference a
// reallocated buffer when meshes of different sizes are built within a frame. 256K triangles is far above
// any realistic shadow caster; oversized meshes are skipped (their shadows fall back to "all lit").
inline constexpr u32 s_BvhMaxPrimitivesPerMesh = 262144u;

// CPU mirror of the shader NwbBvhNode (std430, 32 bytes): AABB + child node indices, or a leaf-flagged
// primitive index in leftChild with rightChild = primitive count (see bvh_common.slangi).
struct NwbBvhNodeGpu{
    Float3UInt aabbMinLeftChild;
    Float3UInt aabbMaxRightChild;
};
static_assert(sizeof(NwbBvhNodeGpu) == 32u, "NwbBvhNodeGpu must match the shader NwbBvhNode std430 layout");

// CPU mirror of the shader NwbBvhBuildPushConstants (shared by the morton / topology / fit kernels).
struct BvhBuildPushConstants{
    u32 primitiveCount = 0u;
    u32 internalCount = 0u;
    u32 refitMode = 0u;
    u32 pad0 = 0u;
    Float4 aabbMin = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 aabbMax = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(sizeof(BvhBuildPushConstants) == sizeof(u32) * 4u + sizeof(Float4) * 2u, "BvhBuildPushConstants must match the shader NwbBvhBuildPushConstants layout");

// Leaf-flag / sentinel mirrors of the bvh_common.slangi shader constants (CPU-side BVH validation + clears).
inline constexpr u32 s_BvhLeafFlag = 0x80000000u;
inline constexpr u32 s_BvhInvalidIndex = 0xFFFFFFFFu;

// Initial scene/instance BVH instance capacity; grows by doubling like the hardware TLAS.
inline constexpr usize s_SceneBvhInitialInstanceCapacity = 64u;

// CPU mirror of the per-instance record the software shadow traversal (U5) consumes. Holds the affine
// world->object transform (so a world-space ray can be pushed into each instance's object space) plus the
// per-mesh BVH / geometry references resolved when traversal is wired. 64 bytes / std430-friendly.
struct SceneSwBvhInstanceGpu{
    Float34 worldToObject{};   // affine world->object (row-major 3x4); pushes a world ray into object space
    u32 meshIndex = 0u;        // slot into the parallel per-mesh node/position/index descriptor arrays
    u32 primitiveCount = 0u;   // triangle count of the referenced mesh (sanity bound)
    u32 pad0 = 0u;
    u32 pad1 = 0u;
};
static_assert(sizeof(SceneSwBvhInstanceGpu) == sizeof(Float34) + sizeof(u32) * 4u, "SceneSwBvhInstanceGpu must stay a tight 64-byte record");

// CPU mirror of the software shadow traversal push constants (see shadow_sw_traversal_cs.slang).
struct SwShadowPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 pad0 = 0u;
};
static_assert(sizeof(SwShadowPushConstants) == sizeof(u32) * 4u, "SwShadowPushConstants must match the shader push-constant layout");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_raytracing_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Extracts the axis-th component (0=x, 1=y, 2=z) of a SIMD vector.
[[nodiscard]] f32 SceneBvhAxisComponent(const SIMDVector value, const u32 axis)noexcept{
    return axis == 0u ? VectorGetX(value) : (axis == 1u ? VectorGetY(value) : VectorGetZ(value));
}

// Recursively builds a binary BVH over the [lo, hi) slice of the instance-index permutation, appending nodes
// to `nodes` (the first node appended for the whole range is the root, index 0). It splits at the spatial
// median of the largest centroid-extent axis, falling back to the count median when a spatial split puts
// every instance on one side. Leaves store NWB_BVH_LEAF_FLAG | instanceIndex + the instance world AABB;
// internal nodes store child node indices + the unioned box — the exact NwbBvhNode layout the per-mesh build
// produces, so the GPU traversal is uniform across the scene BVH and every per-mesh BVH.
u32 BuildSceneBvhNode(
    u32* indices,
    const u32 lo,
    const u32 hi,
    const Float4* instanceAabbMin,
    const Float4* instanceAabbMax,
    const Float4* instanceCentroid,
    Vector<NwbBvhNodeGpu, Core::Alloc::ScratchArena>& nodes
){
    const u32 nodeIndex = static_cast<u32>(nodes.size());
    nodes.push_back(NwbBvhNodeGpu{});

    if((hi - lo) == 1u){
        const u32 instance = indices[lo];
        StoreFloatInt(LoadFloat(instanceAabbMin[instance]), s_BvhLeafFlag | instance, &nodes[nodeIndex].aabbMinLeftChild);
        StoreFloatInt(LoadFloat(instanceAabbMax[instance]), 1u, &nodes[nodeIndex].aabbMaxRightChild);
        return nodeIndex;
    }

    SIMDVector centroidMin = VectorReplicate(1e30f);
    SIMDVector centroidMax = VectorReplicate(-1e30f);
    for(u32 i = lo; i < hi; ++i){
        const SIMDVector centroid = LoadFloat(instanceCentroid[indices[i]]);
        centroidMin = VectorMin(centroidMin, centroid);
        centroidMax = VectorMax(centroidMax, centroid);
    }

    const SIMDVector centroidExtent = VectorSubtract(centroidMax, centroidMin);
    u32 axis = 0u;
    f32 axisExtent = VectorGetX(centroidExtent);
    if(VectorGetY(centroidExtent) > axisExtent){ axis = 1u; axisExtent = VectorGetY(centroidExtent); }
    if(VectorGetZ(centroidExtent) > axisExtent){ axis = 2u; }

    const f32 splitValue = 0.5f * (SceneBvhAxisComponent(centroidMin, axis) + SceneBvhAxisComponent(centroidMax, axis));

    u32 mid = lo;
    for(u32 i = lo; i < hi; ++i){
        if(SceneBvhAxisComponent(LoadFloat(instanceCentroid[indices[i]]), axis) < splitValue){
            const u32 swap = indices[i];
            indices[i] = indices[mid];
            indices[mid] = swap;
            ++mid;
        }
    }
    if(mid == lo || mid == hi)
        mid = lo + (hi - lo) / 2u; // degenerate spatial split (coincident centroids) -> count median; still correct

    const u32 leftChild = BuildSceneBvhNode(indices, lo, mid, instanceAabbMin, instanceAabbMax, instanceCentroid, nodes);
    const u32 rightChild = BuildSceneBvhNode(indices, mid, hi, instanceAabbMin, instanceAabbMax, instanceCentroid, nodes);

    const SIMDVector boxMin = VectorMin(LoadFloatInt(nodes[leftChild].aabbMinLeftChild), LoadFloatInt(nodes[rightChild].aabbMinLeftChild));
    const SIMDVector boxMax = VectorMax(LoadFloatInt(nodes[leftChild].aabbMaxRightChild), LoadFloatInt(nodes[rightChild].aabbMaxRightChild));
    StoreFloatInt(boxMin, leftChild, &nodes[nodeIndex].aabbMinLeftChild);
    StoreFloatInt(boxMax, rightChild, &nodes[nodeIndex].aabbMaxRightChild);
    return nodeIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererRayTracingSystem::RendererRayTracingSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase<RendererSystem>(renderer)
{}


void RendererRayTracingSystem::logCapabilityOnce(){
    if(rayTracingState().m_capabilityLogged)
        return;

    rayTracingState().m_capabilityLogged = true;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: ray tracing capability - accel struct {}, pipeline {}, ray query {}")
        , graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        , graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)
        , graphics().queryFeatureSupport(Core::Feature::RayQuery)
    );

#if defined(NWB_DEBUG)
    runBvhSortSelfTest();
    runBvhBuildSelfTest();
    runSceneBvhSelfTest();
#endif
}

bool RendererRayTracingSystem::buildPendingMeshBlas(Core::CommandList& commandList){
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();

        // Runtime (skinned/deforming) meshes change their vertex positions every
        // frame, so their BLAS is rebuilt from the freshly skinned positions each
        // frame. Static meshes build a BLAS once and clear the pending flag.
        if(meshResources.runtimeMesh){
            if(!buildMeshBlas(commandList, meshResources))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: runtime mesh BLAS build failed"));
            continue;
        }

        if(!meshResources.blasBuildPending)
            continue;
        if(buildMeshBlas(commandList, meshResources))
            meshResources.blasBuildPending = false;
    }
    return true;
}

bool RendererRayTracingSystem::buildPendingMeshSwBvh(Core::CommandList& commandList){
    // The software BVH is the shadow fallback for devices without hardware ray tracing. When accel structs
    // are available buildPendingMeshBlas handles shadows and this path stays idle.
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    bool allBuildsReady = true;
    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();

        // Runtime (skinned/deforming) meshes update their software BVH every frame from the freshly skinned
        // positions; static meshes build once and clear the pending flag.
        if(meshResources.runtimeMesh){
            if(!updateMeshSwBvh(commandList, meshResources)){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: runtime mesh '{}' software BVH update failed")
                    , StringConvert(meshResources.meshName.c_str())
                );
                allBuildsReady = false;
            }
            continue;
        }

        if(!meshResources.swBvhBuildPending)
            continue;
        if(updateMeshSwBvh(commandList, meshResources))
            meshResources.swBvhBuildPending = false;
        else{
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: static mesh '{}' software BVH build failed")
                , StringConvert(meshResources.meshName.c_str())
            );
            allBuildsReady = false;
        }
    }
    return allBuildsReady;
}

bool RendererRayTracingSystem::buildSceneTlas(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena> instances{ scratchArena };
    instances.reserve(rendererView.candidateCount());

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!meshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        const bool meshReady = resolvedMesh.runtime
            ? m_renderer.meshSystem().findRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh)
            : m_renderer.meshSystem().findMeshResources(resolvedMesh.mesh, mesh)
        ;
        if(!meshReady || !mesh || !mesh->blas)
            continue;

        Core::RayTracingInstanceDesc instanceDesc;
        instanceDesc.setBLAS(mesh->blas.get());
        instanceDesc.setInstanceID(static_cast<u32>(instances.size()));
        instanceDesc.setInstanceMask(0xFFu);

        if(const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity)){
            // Compose object->world (T * R(quat) * S) and store it as the instance's row-major 3x4 transform;
            // the engine's column-vector SIMDMatrix rows map directly onto AffineTransform (= Float34).
            const SIMDMatrix instanceWorld = MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            );
            StoreFloat(instanceWorld, &instanceDesc.transform);
        }

        instances.push_back(instanceDesc);
    }

    if(instances.empty()){
        rayTracingState().m_tlasDeviceAddress = 0u;
        return false;
    }

    if(!rayTracingState().m_tlas || rayTracingState().m_tlasMaxInstances < instances.size()){
        usize capacity = rayTracingState().m_tlasMaxInstances > 0u ? rayTracingState().m_tlasMaxInstances : s_TlasInitialInstanceCapacity;
        while(capacity < instances.size())
            capacity *= 2u;

        Core::RayTracingAccelStructDesc accelStructDesc(arena());
        accelStructDesc.setTopLevelMaxInstances(capacity);
        accelStructDesc.setBuildFlags(Core::RayTracingAccelStructBuildFlags::PreferFastTrace);
        accelStructDesc.setDebugName(Name("scene_tlas"));

        auto* device = graphics().getDevice();
        Core::RayTracingAccelStructHandle tlas = device->createAccelStruct(accelStructDesc);
        if(!tlas){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene TLAS (capacity {})")
                , static_cast<u64>(capacity)
            );
            return false;
        }
        rayTracingState().m_tlas = Move(tlas);
        rayTracingState().m_tlasMaxInstances = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created scene TLAS (capacity {} instances)")
            , static_cast<u64>(capacity)
        );
    }

    commandList.buildTopLevelAccelStruct(
        rayTracingState().m_tlas.get(),
        instances.data(),
        instances.size(),
        Core::RayTracingAccelStructBuildFlags::PreferFastTrace
    );
    rayTracingState().m_tlasDeviceAddress = rayTracingState().m_tlas->getDeviceAddress();
    return true;
}

bool RendererRayTracingSystem::buildSceneSwBvh(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    // Software scene/instance BVH (TLAS-analog) — only the no-hardware-ray-tracing fallback builds it; the
    // hardware path uses buildSceneTlas instead.
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    // Per-instance GPU records + the world-space AABBs / centroids the CPU build consumes (kept parallel so
    // the BVH leaf payload indexes straight into the uploaded instance buffer).
    Vector<SceneSwBvhInstanceGpu, Core::Alloc::ScratchArena> instances{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMin{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMax{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceCentroid{ scratchArena };
    instances.reserve(candidateCount);
    instanceAabbMin.reserve(candidateCount);
    instanceAabbMax.reserve(candidateCount);
    instanceCentroid.reserve(candidateCount);

    // Reset the per-frame distinct-mesh table; the gather repopulates it (slot k -> mesh k's buffers) for the
    // per-mesh descriptor arrays the traversal binds.
    rayTracingState().m_swShadowMeshCount = 0u;

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!meshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        const bool meshReady = resolvedMesh.runtime
            ? m_renderer.meshSystem().findRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh)
            : m_renderer.meshSystem().findMeshResources(resolvedMesh.mesh, mesh)
        ;
        // Only instances whose per-mesh software BVH + geometry are built (and that have valid object-space
        // bounds) can be traced.
        if(!meshReady || !mesh || !mesh->swBvhNodeBuffer || !mesh->positionBuffer || !mesh->triangleIndexBuffer || !mesh->csgLocalBounds.valid())
            continue;

        // Dedupe to a per-mesh descriptor-array slot: instances sharing a mesh share its node/position/index
        // buffers. Once the per-frame mesh cap is reached, the instance casts no software shadow this frame.
        Core::Buffer* meshNodeBuffer = mesh->swBvhNodeBuffer.get();
        u32 meshSlot = NWB_SW_SHADOW_MAX_MESHES;
        for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
            if(rayTracingState().m_swShadowMeshNodeBuffers[slot] == meshNodeBuffer){
                meshSlot = slot;
                break;
            }
        }
        if(meshSlot == NWB_SW_SHADOW_MAX_MESHES){
            if(rayTracingState().m_swShadowMeshCount >= NWB_SW_SHADOW_MAX_MESHES)
                continue;
            meshSlot = rayTracingState().m_swShadowMeshCount++;
            rayTracingState().m_swShadowMeshNodeBuffers[meshSlot] = meshNodeBuffer;
            rayTracingState().m_swShadowMeshPositionBuffers[meshSlot] = mesh->positionBuffer.get();
            rayTracingState().m_swShadowMeshIndexBuffers[meshSlot] = mesh->triangleIndexBuffer.get();
        }

        // object->world from the decomposed transform (identity when absent), matching buildSceneTlas.
        SIMDMatrix objectToWorld = MatrixIdentity();
        if(const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity)){
            objectToWorld = MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            );
        }
        SIMDVector determinant;
        const SIMDMatrix worldToObject = MatrixInverse(&determinant, objectToWorld);

        // World AABB = bounds of the 8 object-space corners transformed to world space (exact under rotation).
        const SIMDVector localMin = LoadFloatInt(mesh->csgLocalBounds.minBounds);
        const SIMDVector localMax = LoadFloatInt(mesh->csgLocalBounds.maxBounds);
        const f32 minX = VectorGetX(localMin), minY = VectorGetY(localMin), minZ = VectorGetZ(localMin);
        const f32 maxX = VectorGetX(localMax), maxY = VectorGetY(localMax), maxZ = VectorGetZ(localMax);
        SIMDVector worldMin = VectorReplicate(1e30f);
        SIMDVector worldMax = VectorReplicate(-1e30f);
        for(u32 corner = 0u; corner < 8u; ++corner){
            const SIMDVector localCorner = VectorSet(
                (corner & 1u) != 0u ? maxX : minX,
                (corner & 2u) != 0u ? maxY : minY,
                (corner & 4u) != 0u ? maxZ : minZ,
                1.0f
            );
            const SIMDVector worldCorner = Vector3TransformCoord(localCorner, objectToWorld);
            worldMin = VectorMin(worldMin, worldCorner);
            worldMax = VectorMax(worldMax, worldCorner);
        }

        Float4 aabbMinStore, aabbMaxStore, centroidStore;
        StoreFloat(worldMin, &aabbMinStore);
        StoreFloat(worldMax, &aabbMaxStore);
        StoreFloat(VectorScale(VectorAdd(worldMin, worldMax), 0.5f), &centroidStore);

        SceneSwBvhInstanceGpu instance;
        StoreFloat(worldToObject, &instance.worldToObject);
        instance.meshIndex = meshSlot;
        instance.primitiveCount = mesh->meshletPrimitiveIndexCount / 3u;

        instances.push_back(instance);
        instanceAabbMin.push_back(aabbMinStore);
        instanceAabbMax.push_back(aabbMaxStore);
        instanceCentroid.push_back(centroidStore);
    }

    const u32 instanceCount = static_cast<u32>(instances.size());
    if(instanceCount == 0u){
        // No traceable instances is not a failure: the consumer treats a zero instance count as
        // "nothing occludes" (fully lit), so report success and leave the resident buffers untouched.
        rayTracingState().m_sceneBvhInstanceCount = 0u;
        return true;
    }

    // CPU-build the scene BVH over the gathered instance world AABBs. At TLAS scale (a handful to a few
    // hundred instances) a CPU build + upload is far cheaper than a GPU LBVH dispatch, and the node layout
    // matches the per-mesh BVH so the traversal pass reads scene and mesh BVHs the same way.
    Vector<u32, Core::Alloc::ScratchArena> indices{ scratchArena };
    indices.reserve(instanceCount);
    for(u32 i = 0u; i < instanceCount; ++i)
        indices.push_back(i);

    const usize nodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    Vector<NwbBvhNodeGpu, Core::Alloc::ScratchArena> nodes{ scratchArena };
    nodes.reserve(nodeCount);
    __hidden_raytracing_system::BuildSceneBvhNode(indices.data(), 0u, instanceCount, instanceAabbMin.data(), instanceAabbMax.data(), instanceCentroid.data(), nodes);
    NWB_ASSERT(nodes.size() == nodeCount);

    if(!ensureSceneBvhBuffers(instanceCount))
        return false;

    Core::Buffer* nodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
    Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();

    commandList.setBufferState(nodeBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(nodeBuffer, nodes.data(), nodes.size() * sizeof(NwbBvhNodeGpu));
    commandList.writeBuffer(instanceBuffer, instances.data(), instances.size() * sizeof(SceneSwBvhInstanceGpu));
    commandList.setBufferState(nodeBuffer, Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    rayTracingState().m_sceneBvhInstanceCount = instanceCount;
    return true;
}

bool RendererRayTracingSystem::createShadowVisibilityTarget(DeferredFrameTargets& targets){
    // The shadow-visibility image is the shared output of the shadow subsystem: hardware ray tracing
    // writes a per-light visibility mask here, and the software distance-field fallback will write the
    // same image. The deferred lighting pass always samples it, so it is allocated unconditionally and
    // cleared to "all lit" each frame (then overwritten by whichever backend runs) to keep a single
    // binding/shader path regardless of ray-tracing support.
    targets.shadowVisibilityFormat = Core::Format::R32_UINT;

    Core::TextureDesc visibilityDesc;
    visibilityDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.shadowVisibilityFormat)
        .setInUAV(true)
        .setName("engine/shadow/visibility")
    ;
    targets.shadowVisibility = graphics().createTexture(visibilityDesc);
    if(!targets.shadowVisibility){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow visibility target"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return false;
    if(!ensureShadowPipeline())
        return false;
    if(!ensureShadowBindingSet(targets))
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // The binding set carries the TLAS, the G-buffer SRVs, the scene/light buffers and the visibility
    // UAV; letting the command list derive their states keeps the accel-struct read, shader-resource
    // and unordered-access transitions in one place instead of hand-listing each resource.
    commandList.setResourceStatesForBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.commitBarriers();

    Core::RayTracingState rayTracingPassState;
    rayTracingPassState.setShaderTable(rayTracingState().m_shadowShaderTable.get());
    rayTracingPassState.addBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.setRayTracingState(rayTracingPassState);

    Core::RayTracingDispatchRaysArguments dispatchArgs;
    dispatchArgs.setDimensions(targets.width, targets.height, 1u);
    commandList.dispatchRays(dispatchArgs);
    return true;
}

void RendererRayTracingSystem::clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return;

    // 0xFFFFFFFF = every per-light bit set = fully lit. This is the default the deferred lighting pass
    // samples whenever no shadow backend wrote the image this frame (ray tracing unavailable, no
    // trace-able geometry, or a trace that could not be dispatched).
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureUInt(targets.shadowVisibility.get(), ECSRenderDetail::s_FramebufferSubresources, 0xFFFFFFFFu);
}

bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Software shadow traversal — the no-hardware-ray-tracing fallback that consumes the per-frame software
    // scene/instance BVH (buildSceneSwBvh) + the per-mesh BVHs. When hardware accel structs are available
    // renderShadowVisibility handles shadows and this path stays idle.
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;
    if(!targets.shadowVisibility)
        return false;
    // No software scene BVH this frame (no traceable instances) -> the caller clears the mask to all-lit.
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhInstanceCount == 0u)
        return false;
    if(!ensureSwShadowPipeline())
        return false;
    if(!ensureSwShadowBindingSet(targets))
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // The per-mesh BVH node buffers were left in UnorderedAccess by the build pass; move each distinct mesh's
    // node + geometry buffers to ShaderResource for the traversal reads. The scene node buffer was already
    // uploaded as a shader resource by buildSceneSwBvh; setResourceStatesForBindingSet derives the rest
    // (G-buffer SRVs, visibility UAV).
    for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
        commandList.setBufferState(rayTracingState().m_swShadowMeshNodeBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_swShadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_swShadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
    }
    commandList.setResourceStatesForBindingSet(rayTracingState().m_swShadowBindingSet.get());
    commandList.commitBarriers();

    SwShadowPushConstants pushConstants;
    pushConstants.width = targets.width;
    pushConstants.height = targets.height;
    pushConstants.instanceCount = rayTracingState().m_sceneBvhInstanceCount;

    Core::ComputeState computeState;
    computeState.setPipeline(rayTracingState().m_swShadowPipeline.get());
    computeState.addBindingSet(rayTracingState().m_swShadowBindingSet.get());
    commandList.setComputeState(computeState);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE)),
        1u
    );

    if(!rayTracingState().m_swShadowDispatchLogged){
        rayTracingState().m_swShadowDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched software shadow traversal ({}x{}, {} instances)")
            , static_cast<u64>(targets.width)
            , static_cast<u64>(targets.height)
            , static_cast<u64>(rayTracingState().m_sceneBvhInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::buildMeshBlas(Core::CommandList& commandList, MeshResources& meshResources){
    if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer)
        return false;

    const Core::BufferDesc& positionDesc = meshResources.positionBuffer->getDescription();
    if(positionDesc.structStride == 0u || meshResources.meshletPrimitiveIndexCount == 0u)
        return false;

    const u32 vertexStride = static_cast<u32>(positionDesc.structStride);
    const u32 vertexCount = static_cast<u32>(positionDesc.byteSize / positionDesc.structStride);

    Core::RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(meshResources.positionBuffer.get())
        .setVertexFormat(Core::Format::RGB32_FLOAT)
        .setVertexStride(vertexStride)
        .setVertexCount(vertexCount)
        .setIndexBuffer(meshResources.triangleIndexBuffer.get())
        .setIndexFormat(Core::Format::R32_UINT)
        .setIndexCount(meshResources.meshletPrimitiveIndexCount)
    ;

    Core::RayTracingGeometryDesc geometry;
    geometry
        .setTriangles(triangles)
        .setFlags(Core::RayTracingGeometryFlags::Opaque)
    ;

    // Runtime (skinned) meshes keep a single resident BLAS built with AllowUpdate and refit it in
    // place from the freshly skinned positions each frame, forcing a full rebuild every
    // s_BlasMaxRefitsBeforeRebuild frames to restore BVH quality. Static meshes build once.
    Core::RayTracingAccelStructBuildFlags::Mask buildFlags = Core::RayTracingAccelStructBuildFlags::PreferFastTrace;
    if(meshResources.runtimeMesh)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::AllowUpdate;

    const bool firstBuild = !meshResources.blas;
    if(firstBuild){
        Core::RayTracingAccelStructDesc accelStructDesc(arena());
        accelStructDesc.addBottomLevelGeometry(geometry);
        accelStructDesc.setBuildFlags(buildFlags);
        accelStructDesc.setDebugName(DeriveName(meshResources.meshName, AStringView(":blas")));

        auto* device = graphics().getDevice();
        Core::RayTracingAccelStructHandle blas = device->createAccelStruct(accelStructDesc);
        if(!blas){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BLAS for mesh '{}'")
                , StringConvert(meshResources.meshName.c_str())
            );
            return false;
        }
        meshResources.blas = Move(blas);
        meshResources.blasRefitsSinceRebuild = 0u;
    }

    const bool performRefit =
        meshResources.runtimeMesh
        && !firstBuild
        && meshResources.blasRefitsSinceRebuild < s_BlasMaxRefitsBeforeRebuild
    ;
    if(performRefit)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::PerformUpdate;

    commandList.setBufferState(meshResources.positionBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
    commandList.setBufferState(meshResources.triangleIndexBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
    commandList.commitBarriers();

    commandList.buildBottomLevelAccelStruct(meshResources.blas.get(), &geometry, 1u, buildFlags);

    meshResources.blasRefitsSinceRebuild = performRefit ? (meshResources.blasRefitsSinceRebuild + 1u) : 0u;

    if(firstBuild){
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built BLAS for mesh '{}' (runtime {}, {} vertices, {} indices)")
            , StringConvert(meshResources.meshName.c_str())
            , meshResources.runtimeMesh
            , static_cast<u64>(vertexCount)
            , static_cast<u64>(meshResources.meshletPrimitiveIndexCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureShadowPipeline(){
    if(rayTracingState().m_shadowPipeline)
        return true;
    if(rayTracingState().m_shadowPipelineFailed)
        return false;
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::AllRayTracing);
        layoutDesc.addItem(Core::BindingLayoutItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT, 1));

        rayTracingState().m_shadowBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
            rayTracingState().m_shadowPipelineFailed = true;
            return false;
        }
    }

    Core::ShaderHandle raygenShader;
    Core::ShaderHandle missShader;
    Core::ShaderHandle closestHitShader;
    if(
        !m_renderer.shaderSystem().loadShader(raygenShader, AssetsGraphicsShadow::s_RaygenShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::RayGeneration, "ECSRender_ShadowRaygen")
        || !m_renderer.shaderSystem().loadShader(missShader, AssetsGraphicsShadow::s_MissShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Miss, "ECSRender_ShadowMiss")
        || !m_renderer.shaderSystem().loadShader(closestHitShader, AssetsGraphicsShadow::s_ClosestHitShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::ClosestHit, "ECSRender_ShadowClosestHit")
    ){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    Core::RayTracingPipelineDesc pipelineDesc(arena());
    pipelineDesc.setMaxPayloadSize(static_cast<u32>(sizeof(f32)));
    pipelineDesc.setMaxRecursionDepth(1u);
    pipelineDesc.addBindingLayout(rayTracingState().m_shadowBindingLayout);

    Core::RayTracingPipelineShaderDesc raygenDesc(arena());
    raygenDesc.setShader(raygenShader).setExportName("ShadowRayGen");
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc(arena());
    missDesc.setShader(missShader).setExportName("ShadowMiss");
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc(arena());
    hitGroupDesc.setClosestHitShader(closestHitShader).setExportName("ShadowHitGroup");
    pipelineDesc.addHitGroup(hitGroupDesc);

    rayTracingState().m_shadowPipeline = device->createRayTracingPipeline(pipelineDesc);
    if(!rayTracingState().m_shadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT shadow pipeline"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    Core::RayTracingShaderTableHandle shaderTable = rayTracingState().m_shadowPipeline->createShaderTable();
    if(!shaderTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT shadow shader table"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }
    shaderTable->setRayGenerationShader("ShadowRayGen");
    shaderTable->addMissShader("ShadowMiss");
    shaderTable->addHitGroup("ShadowHitGroup");
    rayTracingState().m_shadowShaderTable = Move(shaderTable);

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RT shadow pipeline + shader table"));
    return true;
}

bool RendererRayTracingSystem::ensureShadowBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowBindingLayout);
    NWB_ASSERT(rayTracingState().m_tlas);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);

    // The TLAS handle is the only binding input that can change without a target resize (it is
    // recreated when the live instance count outgrows its capacity); a resize resets the binding set
    // via resetDeferredFrameTargets, so caching against the TLAS pointer covers both rebuild triggers.
    const Core::RayTracingAccelStruct* tlas = rayTracingState().m_tlas.get();
    if(
        rayTracingState().m_shadowBindingSet
        && rayTracingState().m_shadowBindingSetTlas == tlas
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, rayTracingState().m_tlas.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT,
        targets.shadowVisibility.get(),
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    auto* device = graphics().getDevice();
    rayTracingState().m_shadowBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_shadowBindingLayout);
    if(!rayTracingState().m_shadowBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding set"));
        rayTracingState().m_shadowBindingSetTlas = nullptr;
        return false;
    }
    rayTracingState().m_shadowBindingSetTlas = tlas;
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowPipeline(){
    if(rayTracingState().m_swShadowPipeline)
        return true;
    if(rayTracingState().m_swShadowPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_swShadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SW_SHADOW_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_NODES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_NODES, NWB_SW_SHADOW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_POSITIONS, NWB_SW_SHADOW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_INDICES, NWB_SW_SHADOW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SwShadowPushConstants)));

        rayTracingState().m_swShadowBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_swShadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding layout"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_swShadowShader,
        AssetsGraphicsShadow::s_SwTraversalShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SwShadowTraversal"
    )){
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_swShadowShader)
        .addBindingLayout(rayTracingState().m_swShadowBindingLayout)
    ;
    rayTracingState().m_swShadowPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_swShadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_swShadowBindingLayout);
    NWB_ASSERT(rayTracingState().m_sceneBvhNodeBuffer);
    NWB_ASSERT(rayTracingState().m_sceneInstanceBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowMeshCount > 0u);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);

    // Rebuild when any binding input that can change without a full invalidate changes: the scene node /
    // instance buffers (recreated when they outgrow their capacity), the visibility target (recreated on
    // resize, which also resets this set via resetDeferredFrameTargets), and the distinct-mesh count (the
    // per-mesh descriptor arrays repopulate when the set of traced meshes changes).
    Core::Buffer* sceneNodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
    Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();
    const Core::Texture* visibilityTarget = targets.shadowVisibility.get();
    const u32 meshCount = rayTracingState().m_swShadowMeshCount;
    if(
        rayTracingState().m_swShadowBindingSet
        && rayTracingState().m_swShadowBindingSetSceneNodes == sceneNodeBuffer
        && rayTracingState().m_swShadowBindingSetInstances == instanceBuffer
        && rayTracingState().m_swShadowBindingSetVisibility == visibilityTarget
        && rayTracingState().m_swShadowBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SW_SHADOW_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_NODES, sceneNodeBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT,
        targets.shadowVisibility.get(),
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_INSTANCES, instanceBuffer));

    // Per-mesh descriptor arrays: bind every slot (the shader only indexes meshIndex < meshCount). Unused
    // tail slots are padded with the last real mesh so a non-bindless array has no unbound descriptors.
    for(u32 slot = 0u; slot < NWB_SW_SHADOW_MAX_MESHES; ++slot){
        const u32 source = (slot < meshCount) ? slot : (meshCount - 1u);
        bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_NODES, rayTracingState().m_swShadowMeshNodeBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_POSITIONS, rayTracingState().m_swShadowMeshPositionBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_INDICES, rayTracingState().m_swShadowMeshIndexBuffers[source]).setArrayElement(slot));
    }

    auto* device = graphics().getDevice();
    rayTracingState().m_swShadowBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_swShadowBindingLayout);
    if(!rayTracingState().m_swShadowBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding set"));
        rayTracingState().m_swShadowBindingSetSceneNodes = nullptr;
        rayTracingState().m_swShadowBindingSetInstances = nullptr;
        rayTracingState().m_swShadowBindingSetVisibility = nullptr;
        rayTracingState().m_swShadowBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_swShadowBindingSetSceneNodes = sceneNodeBuffer;
    rayTracingState().m_swShadowBindingSetInstances = instanceBuffer;
    rayTracingState().m_swShadowBindingSetVisibility = visibilityTarget;
    rayTracingState().m_swShadowBindingSetMeshCount = meshCount;
    return true;
}

bool RendererRayTracingSystem::ensureBvhSortPipeline(){
    if(rayTracingState().m_bvhSortPipeline)
        return true;
    if(rayTracingState().m_bvhSortPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhSortBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_KEYS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_PAYLOAD, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhSortPushConstants)));

        rayTracingState().m_bvhSortBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_bvhSortBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding layout"));
            rayTracingState().m_bvhSortPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_bvhSortShader,
        AssetsGraphicsBvh::s_BitonicSortShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_BvhBitonicSort"
    )){
        rayTracingState().m_bvhSortPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_bvhSortShader)
        .addBindingLayout(rayTracingState().m_bvhSortBindingLayout)
    ;
    rayTracingState().m_bvhSortPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_bvhSortPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort compute pipeline"));
        rayTracingState().m_bvhSortPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhSortBuffers(usize paddedCount){
    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhSortKeysBuffer || rayTracingState().m_bvhSortCapacity < paddedCount){
        usize capacity = rayTracingState().m_bvhSortCapacity > 0u ? rayTracingState().m_bvhSortCapacity : s_BvhSortInitialCapacity;
        while(capacity < paddedCount)
            capacity *= 2u;

        Core::BufferDesc keysBufferDesc;
        keysBufferDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("bvh_sort_keys"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_bvhSortKeysBuffer = graphics().createBuffer(keysBufferDesc);
        if(!rayTracingState().m_bvhSortKeysBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort keys buffer"));
            return false;
        }

        Core::BufferDesc payloadBufferDesc;
        payloadBufferDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("bvh_sort_payload"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_bvhSortPayloadBuffer = graphics().createBuffer(payloadBufferDesc);
        if(!rayTracingState().m_bvhSortPayloadBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort payload buffer"));
            return false;
        }

        rayTracingState().m_bvhSortCapacity = capacity;
        rayTracingState().m_bvhSortBindingSet.reset();
    }

    const Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    if(
        rayTracingState().m_bvhSortBindingSet
        && rayTracingState().m_bvhSortBindingSetKeys == keysBuffer
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_KEYS, rayTracingState().m_bvhSortKeysBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_PAYLOAD, rayTracingState().m_bvhSortPayloadBuffer.get()));

    rayTracingState().m_bvhSortBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_bvhSortBindingLayout);
    if(!rayTracingState().m_bvhSortBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding set"));
        rayTracingState().m_bvhSortBindingSetKeys = nullptr;
        return false;
    }
    rayTracingState().m_bvhSortBindingSetKeys = keysBuffer;
    return true;
}

bool RendererRayTracingSystem::bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount){
    NWB_ASSERT(rayTracingState().m_bvhSortPipeline);
    NWB_ASSERT(rayTracingState().m_bvhSortBindingSet);
    NWB_ASSERT(rayTracingState().m_bvhSortKeysBuffer);
    NWB_ASSERT(rayTracingState().m_bvhSortPayloadBuffer);

    // paddedCount must be a power of two and a multiple of the dispatch group size; the caller fills the
    // sort buffers to it and pads the tail with sentinel keys that sort to the end.
    if(paddedCount < static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE))
        return false;

    Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = rayTracingState().m_bvhSortPayloadBuffer.get();

    // Each (sequenceSize, compareDistance) step reads and writes the same buffers, so consecutive steps
    // must be serialized with UAV barriers: enable per-buffer UAV barriers, then commit one per step.
    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);

    const u32 groupCount = paddedCount / static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);
    for(u32 sequenceSize = 2u; sequenceSize <= paddedCount; sequenceSize <<= 1u){
        for(u32 compareDistance = sequenceSize >> 1u; compareDistance > 0u; compareDistance >>= 1u){
            Core::ComputeState computeState;
            computeState.setPipeline(rayTracingState().m_bvhSortPipeline.get());
            computeState.addBindingSet(rayTracingState().m_bvhSortBindingSet.get());
            commandList.setComputeState(computeState);

            BvhSortPushConstants pushConstants;
            pushConstants.elementCount = elementCount;
            pushConstants.compareDistance = compareDistance;
            pushConstants.sequenceSize = sequenceSize;
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
            commandList.dispatch(groupCount, 1u, 1u);

            commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
        }
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhBuildPipeline(){
    if(
        rayTracingState().m_bvhMortonPipeline
        && rayTracingState().m_bvhTopologyPipeline
        && rayTracingState().m_bvhFitPipeline
    )
        return true;
    if(rayTracingState().m_bvhBuildPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_POSITIONS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_TRIANGLE_INDICES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_KEYS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PAYLOAD, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_NODES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PARENT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_VISIT_COUNTER, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhBuildPushConstants)));

        rayTracingState().m_bvhBuildBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_bvhBuildBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build binding layout"));
            rayTracingState().m_bvhBuildPipelineFailed = true;
            return false;
        }
    }

    const auto createBuildPipeline = [this, device](
        Core::ShaderHandle& shader,
        Core::ComputePipelineHandle& pipeline,
        const Name& shaderName,
        const char* debugLabel
    )->bool{
        if(pipeline)
            return true;
        if(!m_renderer.shaderSystem().loadShader(shader, shaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Compute, debugLabel))
            return false;

        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(shader)
            .addBindingLayout(rayTracingState().m_bvhBuildBindingLayout)
        ;
        pipeline = device->createComputePipeline(pipelineDesc);
        return pipeline != nullptr;
    };

    if(
        !createBuildPipeline(rayTracingState().m_bvhMortonShader, rayTracingState().m_bvhMortonPipeline, AssetsGraphicsBvh::s_BvhMortonShaderName, "ECSRender_BvhMorton")
        || !createBuildPipeline(rayTracingState().m_bvhTopologyShader, rayTracingState().m_bvhTopologyPipeline, AssetsGraphicsBvh::s_BvhTopologyShaderName, "ECSRender_BvhTopology")
        || !createBuildPipeline(rayTracingState().m_bvhFitShader, rayTracingState().m_bvhFitPipeline, AssetsGraphicsBvh::s_BvhFitShaderName, "ECSRender_BvhFit")
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build compute pipeline"));
        rayTracingState().m_bvhBuildPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhVisitCounterBuffer(usize primitiveCount){
    if(rayTracingState().m_bvhVisitCounterBuffer && rayTracingState().m_bvhBuildCapacity >= primitiveCount)
        return true;

    usize capacity = rayTracingState().m_bvhBuildCapacity > 0u ? rayTracingState().m_bvhBuildCapacity : s_BvhBuildInitialCapacity;
    while(capacity < primitiveCount)
        capacity *= 2u;

    // The fit's bottom-up rendezvous counter is SHARED scratch (one u32 per internal node, < N). Meshes are
    // built sequentially within a frame and serialized by barriers, so a single shared counter suffices.
    Core::BufferDesc counterBufferDesc;
    counterBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_visit_counter"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_bvhVisitCounterBuffer = graphics().createBuffer(counterBufferDesc);
    if(!rayTracingState().m_bvhVisitCounterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH visit counter buffer"));
        return false;
    }
    rayTracingState().m_bvhBuildCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer){
    if(nodeBuffer && parentBuffer)
        return true;

    // A binary LBVH over N primitives has exactly 2N-1 nodes (internal [0,N-1) + leaves [N-1,2N-1)). These
    // are PER-MESH and persist across frames (refit reuses the topology), so they are sized exactly to N.
    const usize nodeCount = primitiveCount * 2u - 1u;

    Core::BufferDesc nodeBufferDesc;
    nodeBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount))
        .setStructStride(sizeof(NwbBvhNodeGpu))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_mesh_nodes"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    nodeBuffer = graphics().createBuffer(nodeBufferDesc);
    if(!nodeBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH node buffer"));
        return false;
    }

    Core::BufferDesc parentBufferDesc;
    parentBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * nodeCount))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_mesh_parent"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    parentBuffer = graphics().createBuffer(parentBufferDesc);
    if(!parentBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH parent buffer"));
        nodeBuffer.reset();
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureMeshBvhBindingSet(
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    Core::Buffer* nodeBuffer,
    Core::Buffer* parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(bindingSet)
        return true;

    NWB_ASSERT(rayTracingState().m_bvhBuildBindingLayout);
    NWB_ASSERT(rayTracingState().m_bvhSortKeysBuffer);
    NWB_ASSERT(rayTracingState().m_bvhSortPayloadBuffer);
    NWB_ASSERT(rayTracingState().m_bvhVisitCounterBuffer);
    NWB_ASSERT(positionBuffer);
    NWB_ASSERT(triangleIndexBuffer);
    NWB_ASSERT(nodeBuffer);
    NWB_ASSERT(parentBuffer);

    // The set binds this mesh's per-mesh nodes/parent + the shared sort keys/payload + the shared visit
    // counter + this mesh's raw position/index buffers.
    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_POSITIONS, positionBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_TRIANGLE_INDICES, triangleIndexBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_KEYS, rayTracingState().m_bvhSortKeysBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PAYLOAD, rayTracingState().m_bvhSortPayloadBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_NODES, nodeBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PARENT, parentBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_VISIT_COUNTER, rayTracingState().m_bvhVisitCounterBuffer.get()));

    bindingSet = graphics().getDevice()->createBindingSet(bindingSetDesc, rayTracingState().m_bvhBuildBindingLayout);
    if(!bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH build binding set"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureMeshSwBvhResources(
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount > s_BvhMaxPrimitivesPerMesh){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: mesh exceeds software BVH primitive cap ({} > {}), shadows skipped")
            , static_cast<u64>(primitiveCount)
            , static_cast<u64>(s_BvhMaxPrimitivesPerMesh)
        );
        return false;
    }

    if(!ensureBvhSortPipeline())
        return false;
    if(!ensureBvhBuildPipeline())
        return false;
    // Size the shared sort/counter scratch ONCE to the per-mesh cap (a power of two, so it is itself a valid
    // padded sort length). This keeps the shared buffers stable across builds of different-sized meshes, so
    // the per-mesh binding sets that reference them stay valid; the per-mesh node/parent are sized exactly.
    if(!ensureBvhSortBuffers(s_BvhMaxPrimitivesPerMesh))
        return false;
    if(!ensureBvhVisitCounterBuffer(s_BvhMaxPrimitivesPerMesh))
        return false;
    if(!createMeshBvhStorage(primitiveCount, nodeBuffer, parentBuffer))
        return false;
    return ensureMeshBvhBindingSet(positionBuffer, triangleIndexBuffer, nodeBuffer.get(), parentBuffer.get(), bindingSet);
}

bool RendererRayTracingSystem::buildMeshSwBvh(
    Core::CommandList& commandList,
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    const SIMDVector aabbMin,
    const SIMDVector aabbMax,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount == 0u)
        return false;
    if(!ensureMeshSwBvhResources(positionBuffer, triangleIndexBuffer, primitiveCount, nodeBuffer, parentBuffer, bindingSet))
        return false;

    u32 paddedCount = static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);
    while(paddedCount < primitiveCount)
        paddedCount <<= 1u;

    Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = rayTracingState().m_bvhSortPayloadBuffer.get();
    Core::Buffer* visitCounterBuffer = rayTracingState().m_bvhVisitCounterBuffer.get();
    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();
    Core::BindingSet* meshBindingSet = bindingSet.get();

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.aabbMin = Float4(VectorGetX(aabbMin), VectorGetY(aabbMin), VectorGetZ(aabbMin), 0.0f);
    pushConstants.aabbMax = Float4(VectorGetX(aabbMax), VectorGetY(aabbMax), VectorGetZ(aabbMax), 0.0f);

    // Initialize: sort-key padding to a sentinel that sorts last, parent links to "no parent", and the
    // per-internal-node visit counters to zero (the fit's second-arrival rendezvous).
    commandList.setBufferState(keysBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(meshParentBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearBufferUInt(keysBuffer, 0xFFFFFFFFu);
    commandList.clearBufferUInt(meshParentBuffer, s_BvhInvalidIndex);
    commandList.clearBufferUInt(visitCounterBuffer, 0u);

    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer, true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer, true);

    const auto bvhBuildBarrier = [&commandList, keysBuffer, payloadBuffer, meshNodeBuffer, meshParentBuffer, visitCounterBuffer](){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshParentBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    };

    const auto dispatchBuildKernel = [&commandList, &pushConstants, meshBindingSet](Core::ComputePipeline* pipeline, const u32 groupCount){
        Core::ComputeState computeState;
        computeState.setPipeline(pipeline);
        computeState.addBindingSet(meshBindingSet);
        commandList.setComputeState(computeState);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groupCount, 1u, 1u);
    };

    bvhBuildBarrier();

    dispatchBuildKernel(rayTracingState().m_bvhMortonPipeline.get(), DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();

    if(!bvhBitonicSort(commandList, primitiveCount, paddedCount))
        return false;
    bvhBuildBarrier();

    if(primitiveCount > 1u){
        dispatchBuildKernel(rayTracingState().m_bvhTopologyPipeline.get(), DivideUp(primitiveCount - 1u, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
        bvhBuildBarrier();
    }

    dispatchBuildKernel(rayTracingState().m_bvhFitPipeline.get(), DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();
    return true;
}

bool RendererRayTracingSystem::refitMeshSwBvh(
    Core::CommandList& commandList,
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount == 0u)
        return false;
    if(!ensureMeshSwBvhResources(positionBuffer, triangleIndexBuffer, primitiveCount, nodeBuffer, parentBuffer, bindingSet))
        return false;

    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();
    Core::Buffer* visitCounterBuffer = rayTracingState().m_bvhVisitCounterBuffer.get();

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.refitMode = 1u;

    // A refit reuses the sorted topology, child links, and per-leaf primitive from the last full build, so
    // only the rendezvous counters reset; the fit pass recomputes every box from the current positions.
    commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearBufferUInt(visitCounterBuffer, 0u);

    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer, true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer, true);
    commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(meshParentBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(rayTracingState().m_bvhFitPipeline.get());
    computeState.addBindingSet(bindingSet.get());
    commandList.setComputeState(computeState);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)), 1u, 1u);

    commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();
    return true;
}

bool RendererRayTracingSystem::updateMeshSwBvh(Core::CommandList& commandList, MeshResources& meshResources){
    if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer)
        return false;
    // meshletPrimitiveIndexCount is the reconstructed triangle-index count (3 per triangle).
    if(meshResources.meshletPrimitiveIndexCount == 0u || (meshResources.meshletPrimitiveIndexCount % 3u) != 0u)
        return false;
    const u32 primitiveCount = meshResources.meshletPrimitiveIndexCount / 3u;

    // The morton / topology / fit kernels read positions and triangle indices as raw byte buffers, so move
    // both to ShaderResource before the build/refit dispatches bind them.
    commandList.setBufferState(meshResources.positionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(meshResources.triangleIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // Skinned (runtime) meshes deform every frame: refit the build-pose topology in place from the freshly
    // skinned positions, forcing a full rebuild every s_BlasMaxRefitsBeforeRebuild frames to restore tree
    // quality. Static meshes build once. A mesh's first appearance is always a full build.
    const bool firstBuild = !meshResources.swBvhNodeBuffer;
    const bool performRefit =
        meshResources.runtimeMesh
        && !firstBuild
        && meshResources.swBvhRefitsSinceRebuild < s_BlasMaxRefitsBeforeRebuild
    ;

    bool built = false;
    if(performRefit){
        built = refitMeshSwBvh(
            commandList,
            meshResources.positionBuffer.get(),
            meshResources.triangleIndexBuffer.get(),
            primitiveCount,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhBindingSet
        );
    }
    else{
        const SIMDVector aabbMin = LoadFloatInt(meshResources.csgLocalBounds.minBounds);
        const SIMDVector aabbMax = LoadFloatInt(meshResources.csgLocalBounds.maxBounds);
        built = buildMeshSwBvh(
            commandList,
            meshResources.positionBuffer.get(),
            meshResources.triangleIndexBuffer.get(),
            primitiveCount,
            aabbMin,
            aabbMax,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhBindingSet
        );
    }
    if(!built)
        return false;

    if(firstBuild){
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built software BVH for mesh '{}' (runtime {}, {} triangles)")
            , StringConvert(meshResources.meshName.c_str())
            , meshResources.runtimeMesh
            , static_cast<u64>(primitiveCount)
        );
    }

    meshResources.swBvhRefitsSinceRebuild = performRefit ? (meshResources.swBvhRefitsSinceRebuild + 1u) : 0u;
    return true;
}

bool RendererRayTracingSystem::ensureSceneBvhBuffers(u32 instanceCount){
    // A binary BVH over N instances has 2N-1 nodes. Both buffers are CPU-written each frame and read by the
    // traversal pass, so they are structured SRVs (no UAV) that grow by doubling like the hardware TLAS.
    const usize requiredNodes = static_cast<usize>(instanceCount) * 2u - 1u;
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhNodeCapacity < requiredNodes){
        usize capacity = rayTracingState().m_sceneBvhNodeCapacity > 0u
            ? rayTracingState().m_sceneBvhNodeCapacity
            : (s_SceneBvhInitialInstanceCapacity * 2u - 1u)
        ;
        while(capacity < requiredNodes)
            capacity *= 2u;

        Core::BufferDesc nodeBufferDesc;
        nodeBufferDesc
            .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * capacity))
            .setStructStride(sizeof(NwbBvhNodeGpu))
            .setDebugName(Name("scene_bvh_nodes"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_sceneBvhNodeBuffer = graphics().createBuffer(nodeBufferDesc);
        if(!rayTracingState().m_sceneBvhNodeBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH node buffer"));
            return false;
        }
        rayTracingState().m_sceneBvhNodeCapacity = capacity;
    }

    if(!rayTracingState().m_sceneInstanceBuffer || rayTracingState().m_sceneInstanceCapacity < instanceCount){
        usize capacity = rayTracingState().m_sceneInstanceCapacity > 0u
            ? rayTracingState().m_sceneInstanceCapacity
            : s_SceneBvhInitialInstanceCapacity
        ;
        while(capacity < instanceCount)
            capacity *= 2u;

        Core::BufferDesc instanceBufferDesc;
        instanceBufferDesc
            .setByteSize(static_cast<u64>(sizeof(SceneSwBvhInstanceGpu) * capacity))
            .setStructStride(sizeof(SceneSwBvhInstanceGpu))
            .setDebugName(Name("scene_bvh_instances"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_sceneInstanceBuffer = graphics().createBuffer(instanceBufferDesc);
        if(!rayTracingState().m_sceneInstanceBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH instance buffer"));
            return false;
        }
        rayTracingState().m_sceneInstanceCapacity = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created software scene BVH buffers (capacity {} instances)")
            , static_cast<u64>(capacity)
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::runBvhSortSelfTest(){
    if(rayTracingState().m_bvhSortSelfTestDone)
        return;
    rayTracingState().m_bvhSortSelfTestDone = true;

    // A wrong sort silently corrupts every BVH built on top of it, so verify the kernel directly in debug
    // builds: sort a reversed sequence with an identity payload and read it back. The ascending result must
    // be exactly 0..elementCount-1, with the sentinel-padded tail still non-decreasing.
    constexpr u32 elementCount = 1000u;
    constexpr u32 paddedCount = 1024u;
    static_assert(paddedCount >= static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE), "self-test padded count must cover one dispatch group");

    if(!ensureBvhSortPipeline())
        return;
    if(!ensureBvhSortBuffers(paddedCount))
        return;

    auto* device = graphics().getDevice();

    u32 inputKeys[paddedCount];
    u32 inputPayload[paddedCount];
    for(u32 i = 0u; i < paddedCount; ++i){
        inputKeys[i] = i < elementCount ? (elementCount - 1u - i) : 0xFFFFFFFFu;
        inputPayload[i] = i;
    }

    Core::BufferDesc readbackBufferDesc;
    readbackBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * paddedCount))
        .setCpuAccess(Core::CpuAccessMode::Read)
        .setDebugName(Name("bvh_sort_selftest_readback"))
        .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
    ;
    Core::BufferHandle readbackBuffer = graphics().createBuffer(readbackBufferDesc);
    if(!readbackBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort self-test readback buffer"));
        return;
    }

    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort self-test command list"));
        return;
    }

    commandList->open();
    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->setBufferState(rayTracingState().m_bvhSortPayloadBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->commitBarriers();
    commandList->writeBuffer(rayTracingState().m_bvhSortKeysBuffer.get(), inputKeys, sizeof(inputKeys));
    commandList->writeBuffer(rayTracingState().m_bvhSortPayloadBuffer.get(), inputPayload, sizeof(inputPayload));
    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList->setBufferState(rayTracingState().m_bvhSortPayloadBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();

    if(!bvhBitonicSort(*commandList, elementCount, paddedCount)){
        commandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH sort self-test dispatch setup failed"));
        return;
    }

    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::CopySource);
    commandList->commitBarriers();
    commandList->copyBuffer(readbackBuffer.get(), 0u, rayTracingState().m_bvhSortKeysBuffer.get(), 0u, static_cast<u64>(sizeof(u32) * paddedCount));
    commandList->close();

    Core::CommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH sort self-test wait-for-idle failed"));
        return;
    }

    const u32* sortedKeys = static_cast<const u32*>(device->mapBuffer(readbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!sortedKeys){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH sort self-test readback buffer"));
        return;
    }

    bool sorted = true;
    for(u32 i = 0u; i < elementCount; ++i){
        if(sortedKeys[i] != i){
            sorted = false;
            break;
        }
    }
    for(u32 i = 0u; sorted && (i + 1u) < paddedCount; ++i){
        if(sortedKeys[i] > sortedKeys[i + 1u]){
            sorted = false;
            break;
        }
    }
    device->unmapBuffer(readbackBuffer.get());

    if(sorted)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH bitonic sort self-test PASSED ({} elements)"), elementCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH bitonic sort self-test FAILED"));
}

void RendererRayTracingSystem::runBvhBuildSelfTest(){
    if(rayTracingState().m_bvhBuildSelfTestDone)
        return;
    rayTracingState().m_bvhBuildSelfTestDone = true;

    // Build an LBVH for a known triangle set and validate it on the CPU. A wrong topology can still bound
    // everything correctly, so this checks leaf coverage and child-box nesting, not just the root box.
    constexpr u32 triangleCount = 16u;
    constexpr u32 vertexCount = triangleCount * 3u;
    constexpr u32 floatCount = vertexCount * 3u;
    constexpr u32 nodeCount = triangleCount * 2u - 1u;
    constexpr u32 internalCount = triangleCount - 1u;

    f32 positionData[floatCount];
    u32 indexData[vertexCount];
    for(u32 triangle = 0u; triangle < triangleCount; ++triangle){
        const f32 baseX = static_cast<f32>(triangle);
        const u32 floatBase = triangle * 9u;
        positionData[floatBase + 0u] = baseX;         positionData[floatBase + 1u] = 0.0f;  positionData[floatBase + 2u] = 0.0f;
        positionData[floatBase + 3u] = baseX + 0.6f;  positionData[floatBase + 4u] = 0.8f;  positionData[floatBase + 5u] = 0.1f;
        positionData[floatBase + 6u] = baseX + 0.2f;  positionData[floatBase + 7u] = 0.1f;  positionData[floatBase + 8u] = 0.9f;
    }
    for(u32 index = 0u; index < vertexCount; ++index)
        indexData[index] = index;

    SIMDVector boundsMinVector = VectorReplicate(1e30f);
    SIMDVector boundsMaxVector = VectorReplicate(-1e30f);
    for(u32 vertex = 0u; vertex < vertexCount; ++vertex){
        const SIMDVector position = VectorSet(positionData[vertex * 3u + 0u], positionData[vertex * 3u + 1u], positionData[vertex * 3u + 2u], 0.0f);
        boundsMinVector = VectorMin(boundsMinVector, position);
        boundsMaxVector = VectorMax(boundsMaxVector, position);
    }
    auto* device = graphics().getDevice();

    // Per-mesh BVH storage now lives with the caller (here, the self-test); the build/refit helpers create
    // and reuse these on first use, mirroring how MeshResources will own them on the real path.
    Core::BufferHandle testNodeBuffer;
    Core::BufferHandle testParentBuffer;
    Core::BindingSetHandle testBindingSet;

    Core::BufferDesc positionBufferDesc;
    positionBufferDesc
        .setByteSize(sizeof(positionData))
        .setCanHaveRawViews(true)
        .setDebugName(Name("bvh_build_selftest_positions"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle positionBuffer = graphics().createBuffer(positionBufferDesc);

    Core::BufferDesc indexBufferDesc;
    indexBufferDesc
        .setByteSize(sizeof(indexData))
        .setCanHaveRawViews(true)
        .setDebugName(Name("bvh_build_selftest_indices"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle indexBuffer = graphics().createBuffer(indexBufferDesc);
    if(!positionBuffer || !indexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build self-test geometry buffers"));
        return;
    }

    Core::BufferDesc readbackBufferDesc;
    readbackBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount))
        .setCpuAccess(Core::CpuAccessMode::Read)
        .setDebugName(Name("bvh_build_selftest_readback"))
        .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
    ;
    Core::BufferHandle readbackBuffer = graphics().createBuffer(readbackBufferDesc);
    if(!readbackBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build self-test readback buffer"));
        return;
    }

    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build self-test command list"));
        return;
    }

    commandList->open();
    commandList->setBufferState(positionBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->setBufferState(indexBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->commitBarriers();
    commandList->writeBuffer(positionBuffer.get(), positionData, sizeof(positionData));
    commandList->writeBuffer(indexBuffer.get(), indexData, sizeof(indexData));
    commandList->setBufferState(positionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList->setBufferState(indexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    if(!buildMeshSwBvh(*commandList, positionBuffer.get(), indexBuffer.get(), triangleCount, boundsMinVector, boundsMaxVector, testNodeBuffer, testParentBuffer, testBindingSet)){
        commandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH build self-test build failed"));
        return;
    }

    commandList->setBufferState(testNodeBuffer.get(), Core::ResourceStates::CopySource);
    commandList->commitBarriers();
    commandList->copyBuffer(readbackBuffer.get(), 0u, testNodeBuffer.get(), 0u, static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount));
    commandList->close();

    Core::CommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH build self-test wait-for-idle failed"));
        return;
    }

    const NwbBvhNodeGpu* nodes = static_cast<const NwbBvhNodeGpu*>(device->mapBuffer(readbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!nodes){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH build self-test readback buffer"));
        return;
    }

    bool valid = true;

    // (1) Leaf coverage: every leaf node carries the flag + a unique primitive, and all primitives appear.
    bool primitiveSeen[triangleCount] = {};
    for(u32 leaf = 0u; valid && leaf < triangleCount; ++leaf){
        const NwbBvhNodeGpu& node = nodes[internalCount + leaf];
        if((node.aabbMinLeftChild.w & s_BvhLeafFlag) == 0u){
            valid = false;
            break;
        }
        const u32 primitive = node.aabbMinLeftChild.w & ~s_BvhLeafFlag;
        if(primitive >= triangleCount || primitiveSeen[primitive]){
            valid = false;
            break;
        }
        primitiveSeen[primitive] = true;
    }
    for(u32 primitive = 0u; valid && primitive < triangleCount; ++primitive){
        if(!primitiveSeen[primitive])
            valid = false;
    }

    // (2) Root box matches the input bounds.
    const SIMDVector epsilonVector = VectorReplicate(1e-3f);
    if(valid){
        const SIMDVector rootMin = LoadFloatInt(nodes[0].aabbMinLeftChild);
        const SIMDVector rootMax = LoadFloatInt(nodes[0].aabbMaxRightChild);
        if(
            !Vector3LessOrEqual(VectorAbs(VectorSubtract(rootMin, boundsMinVector)), epsilonVector)
            || !Vector3LessOrEqual(VectorAbs(VectorSubtract(rootMax, boundsMaxVector)), epsilonVector)
        )
            valid = false;
    }

    // (3) Every internal node references valid children and its box contains both child boxes.
    for(u32 internal = 0u; valid && internal < internalCount; ++internal){
        const NwbBvhNodeGpu& node = nodes[internal];
        if(node.aabbMinLeftChild.w >= nodeCount || node.aabbMaxRightChild.w >= nodeCount){
            valid = false;
            break;
        }
        const NwbBvhNodeGpu& left = nodes[node.aabbMinLeftChild.w];
        const NwbBvhNodeGpu& right = nodes[node.aabbMaxRightChild.w];
        const SIMDVector childMin = VectorMin(LoadFloatInt(left.aabbMinLeftChild), LoadFloatInt(right.aabbMinLeftChild));
        const SIMDVector childMax = VectorMax(LoadFloatInt(left.aabbMaxRightChild), LoadFloatInt(right.aabbMaxRightChild));
        if(
            !Vector3LessOrEqual(LoadFloatInt(node.aabbMinLeftChild), VectorAdd(childMin, epsilonVector))
            || !Vector3GreaterOrEqual(LoadFloatInt(node.aabbMaxRightChild), VectorSubtract(childMax, epsilonVector))
        )
            valid = false;
    }

    device->unmapBuffer(readbackBuffer.get());

    if(valid)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH build self-test PASSED ({} triangles, {} nodes)"), triangleCount, nodeCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH build self-test FAILED"));

    // Refit phase: translate every vertex and refit the EXISTING topology. A refit reuses the build-pose
    // tree and only recomputes boxes from the current positions, so the root box must track the translation.
    for(u32 vertex = 0u; vertex < vertexCount; ++vertex)
        positionData[vertex * 3u + 0u] += 5.0f;
    const SIMDVector refitOffset = VectorSet(5.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector refitMin = VectorAdd(boundsMinVector, refitOffset);
    const SIMDVector refitMax = VectorAdd(boundsMaxVector, refitOffset);

    Core::BufferHandle refitReadbackBuffer = graphics().createBuffer(readbackBufferDesc);
    Core::CommandListHandle refitCommandList = device->createCommandList();
    if(!refitReadbackBuffer || !refitCommandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH refit self-test resources"));
        return;
    }

    refitCommandList->open();
    refitCommandList->setBufferState(positionBuffer.get(), Core::ResourceStates::CopyDest);
    refitCommandList->commitBarriers();
    refitCommandList->writeBuffer(positionBuffer.get(), positionData, sizeof(positionData));
    refitCommandList->setBufferState(positionBuffer.get(), Core::ResourceStates::ShaderResource);
    refitCommandList->commitBarriers();

    if(!refitMeshSwBvh(*refitCommandList, positionBuffer.get(), indexBuffer.get(), triangleCount, testNodeBuffer, testParentBuffer, testBindingSet)){
        refitCommandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH refit self-test refit failed"));
        return;
    }

    refitCommandList->setBufferState(testNodeBuffer.get(), Core::ResourceStates::CopySource);
    refitCommandList->commitBarriers();
    refitCommandList->copyBuffer(refitReadbackBuffer.get(), 0u, testNodeBuffer.get(), 0u, static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount));
    refitCommandList->close();

    Core::CommandList* refitCommandLists[] = { refitCommandList.get() };
    device->executeCommandLists(refitCommandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH refit self-test wait-for-idle failed"));
        return;
    }

    const NwbBvhNodeGpu* refitNodes = static_cast<const NwbBvhNodeGpu*>(device->mapBuffer(refitReadbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!refitNodes){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH refit self-test readback buffer"));
        return;
    }
    const bool refitValid =
        Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloatInt(refitNodes[0].aabbMinLeftChild), refitMin)), epsilonVector)
        && Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloatInt(refitNodes[0].aabbMaxRightChild), refitMax)), epsilonVector)
    ;
    device->unmapBuffer(refitReadbackBuffer.get());

    if(refitValid)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH refit self-test PASSED ({} triangles)"), triangleCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH refit self-test FAILED"));
}

void RendererRayTracingSystem::runSceneBvhSelfTest(){
    if(rayTracingState().m_sceneBvhSelfTestDone)
        return;
    rayTracingState().m_sceneBvhSelfTestDone = true;

    // The scene/instance BVH is CPU-built, so validate the builder directly: build a tree over known instance
    // AABBs and check leaf coverage, the root box, and child-box nesting (a wrong topology can still bound
    // everything, so the structural checks matter, not just the root box).
    constexpr u32 instanceCount = 12u;
    constexpr u32 nodeCount = instanceCount * 2u - 1u;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RayTracingBuildArena, 16u * 1024u);

    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMin{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMax{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceCentroid{ scratchArena };
    instanceAabbMin.reserve(instanceCount);
    instanceAabbMax.reserve(instanceCount);
    instanceCentroid.reserve(instanceCount);

    SIMDVector boundsMin = VectorReplicate(1e30f);
    SIMDVector boundsMax = VectorReplicate(-1e30f);
    for(u32 i = 0u; i < instanceCount; ++i){
        const f32 base = static_cast<f32>(i) * 3.0f;
        const SIMDVector boxMin = VectorSet(base, base * 0.25f, -base * 0.5f, 0.0f);
        const SIMDVector boxMax = VectorAdd(boxMin, VectorSet(1.5f, 2.0f, 1.0f, 0.0f));

        Float4 boxMinStore, boxMaxStore, centroidStore;
        StoreFloat(boxMin, &boxMinStore);
        StoreFloat(boxMax, &boxMaxStore);
        StoreFloat(VectorScale(VectorAdd(boxMin, boxMax), 0.5f), &centroidStore);
        instanceAabbMin.push_back(boxMinStore);
        instanceAabbMax.push_back(boxMaxStore);
        instanceCentroid.push_back(centroidStore);

        boundsMin = VectorMin(boundsMin, boxMin);
        boundsMax = VectorMax(boundsMax, boxMax);
    }

    Vector<u32, Core::Alloc::ScratchArena> indices{ scratchArena };
    indices.reserve(instanceCount);
    for(u32 i = 0u; i < instanceCount; ++i)
        indices.push_back(i);

    Vector<NwbBvhNodeGpu, Core::Alloc::ScratchArena> nodes{ scratchArena };
    nodes.reserve(nodeCount);
    __hidden_raytracing_system::BuildSceneBvhNode(indices.data(), 0u, instanceCount, instanceAabbMin.data(), instanceAabbMax.data(), instanceCentroid.data(), nodes);

    bool valid = (nodes.size() == nodeCount);

    // (1) Leaf coverage: every instance appears in exactly one leaf, and every leaf is a valid instance.
    bool instanceSeen[instanceCount] = {};
    u32 leafCount = 0u;
    for(u32 i = 0u; valid && i < nodes.size(); ++i){
        const u32 leftChild = nodes[i].aabbMinLeftChild.w;
        if((leftChild & s_BvhLeafFlag) == 0u)
            continue;
        ++leafCount;
        const u32 instance = leftChild & ~s_BvhLeafFlag;
        if(instance >= instanceCount || instanceSeen[instance]){
            valid = false;
            break;
        }
        instanceSeen[instance] = true;
    }
    if(valid && leafCount != instanceCount)
        valid = false;
    for(u32 i = 0u; valid && i < instanceCount; ++i){
        if(!instanceSeen[i])
            valid = false;
    }

    // (2) Root box == union of all instance AABBs.
    const SIMDVector epsilonVector = VectorReplicate(1e-3f);
    if(valid){
        const SIMDVector rootMin = LoadFloatInt(nodes[0].aabbMinLeftChild);
        const SIMDVector rootMax = LoadFloatInt(nodes[0].aabbMaxRightChild);
        if(
            !Vector3LessOrEqual(VectorAbs(VectorSubtract(rootMin, boundsMin)), epsilonVector)
            || !Vector3LessOrEqual(VectorAbs(VectorSubtract(rootMax, boundsMax)), epsilonVector)
        )
            valid = false;
    }

    // (3) Every internal node references valid children and its box contains both child boxes.
    for(u32 i = 0u; valid && i < nodes.size(); ++i){
        const u32 leftChild = nodes[i].aabbMinLeftChild.w;
        if((leftChild & s_BvhLeafFlag) != 0u)
            continue;
        const u32 rightChild = nodes[i].aabbMaxRightChild.w;
        if(leftChild >= nodes.size() || rightChild >= nodes.size()){
            valid = false;
            break;
        }
        const SIMDVector childMin = VectorMin(LoadFloatInt(nodes[leftChild].aabbMinLeftChild), LoadFloatInt(nodes[rightChild].aabbMinLeftChild));
        const SIMDVector childMax = VectorMax(LoadFloatInt(nodes[leftChild].aabbMaxRightChild), LoadFloatInt(nodes[rightChild].aabbMaxRightChild));
        if(
            !Vector3LessOrEqual(LoadFloatInt(nodes[i].aabbMinLeftChild), VectorAdd(childMin, epsilonVector))
            || !Vector3GreaterOrEqual(LoadFloatInt(nodes[i].aabbMaxRightChild), VectorSubtract(childMax, epsilonVector))
        )
            valid = false;
    }

    if(valid)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: scene BVH self-test PASSED ({} instances, {} nodes)"), instanceCount, nodeCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: scene BVH self-test FAILED"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

