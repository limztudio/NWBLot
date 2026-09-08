// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "graph_dispatch_plan.h"
#include "runtime_cache.h"
#include "submission_state.h"

#include <core/alloc/scratch.h>
#include <core/ecs/system.h>
#include <core/graphics/runtime/render_pass.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/task/gpu/persistent_state.h>
#include <core/task/gpu/types.h>
#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <impl/ecs_mesh/runtime/mesh.h>
#include <impl/ecs_mesh/components.h>
#include <impl/ecs_skeleton/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskRecordContext;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;
struct MeshSkinningRuntimeInstance;
struct RuntimeSkinPayloadScratch;

static_assert(
    SkeletonSkinningMode::LinearBlend == NWB_SKINNED_MESH_SKINNING_MODE_LINEAR_BLEND && SkeletonSkinningMode::DualQuaternion == NWB_SKINNED_MESH_SKINNING_MODE_DUAL_QUATERNION
    , "Skeleton skinning mode values must match the shader ABI"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshSkinningSystem final
    : public Core::ECS::ISystem
    , public Core::IRenderPass
    , public IRuntimeMeshProvider
{
private:
    struct MeshSkinningPushConstants{
        u32 meshletCount = 0;
        u32 skinCount = 0;
        u32 jointCount = 0;
        u32 skinningMode = SkeletonSkinningMode::LinearBlend;
        u32 attributeCount = 0;
        // UniformBuffer heap slot for MeshSkinningBindlessResourceSlots.
        u32 bindlessResourceSlots = 0;
        u32 padding2 = 0;
        u32 padding3 = 0;
    };
    static_assert(sizeof(MeshSkinningPushConstants) == NWB_SKINNED_MESH_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning push constants layout must match the shader ABI");
    static_assert(offsetof(MeshSkinningPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_MESHLET_COUNT, "MeshSkinning meshlet-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, skinCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_SKIN_COUNT, "MeshSkinning skin-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, jointCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_JOINT_COUNT, "MeshSkinning joint-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, skinningMode) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_SKINNING_MODE, "MeshSkinning skinning-mode push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, attributeCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_ATTRIBUTE_COUNT, "MeshSkinning attribute-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, bindlessResourceSlots) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_BINDLESS_RESOURCES_SLOT, "MeshSkinning bindless-resource slot push offset drifted");

    struct MeshletBoundsPushConstants{
        u32 meshletCount = 0;
        // UniformBuffer heap slot for MeshSkinningBindlessResourceSlots.
        u32 bindlessResourceSlots = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
    };
    static_assert(sizeof(MeshletBoundsPushConstants) == NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning bounds push constants layout must match the shader ABI");
    static_assert(offsetof(MeshletBoundsPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT, "MeshSkinning bounds meshlet-count push offset drifted");
    static_assert(offsetof(MeshletBoundsPushConstants, bindlessResourceSlots) == sizeof(u32) * NWB_SKINNED_MESH_BOUNDS_PUSH_BINDLESS_RESOURCES_SLOT, "MeshSkinning bounds bindless-resource slot push offset drifted");

    struct MeshletRepackPushConstants{
        u32 meshletCount = 0;
        // UniformBuffer heap slot for MeshSkinningBindlessResourceSlots.
        u32 bindlessResourceSlots = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
    };
    static_assert(sizeof(MeshletRepackPushConstants) == NWB_SKINNED_MESH_REPACK_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning repack push constants layout must match the shader ABI");
    static_assert(offsetof(MeshletRepackPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_REPACK_PUSH_MESHLET_COUNT, "MeshSkinning repack meshlet-count push offset drifted");
    static_assert(offsetof(MeshletRepackPushConstants, bindlessResourceSlots) == sizeof(u32) * NWB_SKINNED_MESH_REPACK_PUSH_BINDLESS_RESOURCES_SLOT, "MeshSkinning repack bindless-resource slot push offset drifted");

    struct RuntimeBindlessHeapHandles{
        Core::GpuDescriptorHandle resourceSlots = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle restPosition = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle skinnedPosition = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle restNormal = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle skinnedNormal = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle restTangent = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle skinnedTangent = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle meshletDesc = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle positionRefDeltas = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle attributeRefDeltas = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle attributeSkins = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle skinInfluences = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle jointPalette = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle localVertexRefs = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle primitiveIndices = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle meshletBounds = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle attributeBuffer = Core::GpuDescriptorHandle::invalid();
    };

    struct RuntimeResources{
        RuntimeMeshHandle handle;
        u32 editRevision = 0;
        u32 positionCount = 0;
        u32 attributeCount = 0;
        u32 meshletCount = 0;
        u32 skinCount = 0;
        u32 jointCount = 0;
        Core::BufferHandle skinBuffer;
        Core::BufferHandle jointPaletteBuffer;
        Core::BufferHandle bindlessResourceSlotsBuffer;
        MeshSkinningBindlessResourceSlots bindlessResourceSlots;
        RuntimeBindlessHeapHandles bindlessHeapHandles;
        bool bindlessResourceSlotsUploaded = false;

        [[nodiscard]] bool usesSkinning()const{ return skinCount != 0u && jointCount != 0u; }
        [[nodiscard]] bool hasPersistentHeapDescriptors(const bool hasActiveSkin, const bool hasAttributeBuffer)const{
            const auto storageHandle = [](const Core::GpuDescriptorHandle handle){
                return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
            };
            const auto uniformHandle = [](const Core::GpuDescriptorHandle handle){
                return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer;
            };
            const bool common =
                bindlessResourceSlotsBuffer != nullptr
                && uniformHandle(bindlessHeapHandles.resourceSlots)
                && storageHandle(bindlessHeapHandles.restPosition)
                && storageHandle(bindlessHeapHandles.skinnedPosition)
                && storageHandle(bindlessHeapHandles.restNormal)
                && storageHandle(bindlessHeapHandles.skinnedNormal)
                && storageHandle(bindlessHeapHandles.restTangent)
                && storageHandle(bindlessHeapHandles.skinnedTangent)
                && storageHandle(bindlessHeapHandles.meshletDesc)
                && storageHandle(bindlessHeapHandles.positionRefDeltas)
                && storageHandle(bindlessHeapHandles.attributeRefDeltas)
                && storageHandle(bindlessHeapHandles.attributeSkins)
                && storageHandle(bindlessHeapHandles.localVertexRefs)
                && storageHandle(bindlessHeapHandles.primitiveIndices)
                && storageHandle(bindlessHeapHandles.meshletBounds)
            ;
            return common
                && (!hasActiveSkin || (storageHandle(bindlessHeapHandles.skinInfluences) && storageHandle(bindlessHeapHandles.jointPalette)))
                && (!hasAttributeBuffer || storageHandle(bindlessHeapHandles.attributeBuffer))
            ;
        }
    };

    struct TaskGraphSkinningDeformationTask;
    struct TaskGraphSkinningPostDispatchTask;
    struct TaskGraphSkinningFinalizerTask;


public:
    using ShaderPathResolveCallback = Function<
        bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)
    >;


public:
    MeshSkinningSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::GraphicsRuntime& graphics,
        Core::Assets::AssetManager& assetManager,
        IRuntimeMeshRegistry& runtimeMeshRegistry,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~MeshSkinningSystem()override;


public:
    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual bool prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void invalidateResources()override;

    virtual bool resolveRuntimeMesh(Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh)override;
    virtual void markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests)override;

private:
    [[nodiscard]] bool ensureSkinningPipeline();
    [[nodiscard]] bool ensureBoundsPipeline();
    [[nodiscard]] bool ensureRepackPipeline();
    // Declares all frame-local skinning work as one graph-owned primary-Graphics packet: immutable palette/selector
    // uploads and rest-stream copies feed deformation, bounds/repack, and final-state stages whose accepted task
    // commits dirty-state changes.
    [[nodiscard]] bool submitFrameSkinningGraph();
    [[nodiscard]] bool prepareRuntimeMeshResources(
        MeshSkinningRuntimeInstance& instance,
        const SkeletonJointPaletteComponent* jointPalette,
        const SkeletonPoseComponent* skeletonPose,
        Core::Alloc::ScratchArena& scratchArena
    );
    [[nodiscard]] bool recordGraphOwnedSkinningDeformation(
        const MeshSkinningGraphDispatchPlan& plan,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
    [[nodiscard]] bool recordGraphOwnedSkinningPostDispatch(
        const MeshSkinningGraphDispatchPlan& plan,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
    void confirmGraphOwnedSkinningDispatch(const MeshSkinningGraphDispatchPlan& plan)noexcept;
    [[nodiscard]] static bool resolveRestToSkinnedCopyByteCounts(
        const MeshSkinningRuntimeInstance& instance,
        usize& outPositionBytes,
        usize& outNormalBytes,
        usize& outTangentBytes
    );
    void collectLiveSkinningStateBuffers(
        Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outBuffers,
        Core::Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] bool replaceAcceptedSkinningState(
        const Core::CommandListResourceStateHandoff& state,
        Core::Alloc::ScratchArena& scratchArena
    );
    [[nodiscard]] bool ensureRuntimeResources(
        MeshSkinningRuntimeInstance& instance,
        const RuntimeSkinPayloadScratch& payload,
        Core::Alloc::ScratchArena& scratchArena,
        RuntimeResources*& outResources,
        bool& outResourcesRebuilt
    );
    [[nodiscard]] bool createRuntimeResourceBindlessHeapHandles(MeshSkinningRuntimeInstance& instance, RuntimeResources& resources);
    void releaseRuntimeResourceBindlessHeapHandles(RuntimeResources& resources);
    void pruneRuntimeResources();


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    IRuntimeMeshRegistry& m_runtimeMeshRegistry;
    ShaderPathResolveCallback m_shaderPathResolver;
    MeshSkinningRuntimeCache m_runtimeMeshCache;

    HashMap<u64, RuntimeResources, Hasher<u64>, EqualTo<u64>, Core::Alloc::GlobalArena> m_runtimeResources;
    // The graph-runtime cache retains only accepted live skinning resources between graph generations.
    Core::GpuPersistentResourceStateCache m_acceptedSkinningState;
    Core::BindingLayoutHandle m_skinningBindingLayout;
    Core::ShaderHandle m_skinningComputeShader;
    Core::ComputePipelineHandle m_skinningComputePipeline;
    Core::BindingLayoutHandle m_boundsBindingLayout;
    Core::ShaderHandle m_boundsComputeShader;
    Core::ComputePipelineHandle m_boundsComputePipeline;
    Core::BindingLayoutHandle m_repackBindingLayout;
    Core::ShaderHandle m_repackComputeShader;
    Core::ComputePipelineHandle m_repackComputePipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

