// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "graph_dispatch_plan.h"
#include "local_bounds.h"
#include "runtime_cache.h"

#include <impl/ecs_mesh/skinning/submission_state.adhd.h>

#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <impl/assets_mesh/subsystem.adhd.h>
#include <impl/assets_scene/asset.h>

#include <core/alloc/scratch.h>
#include <core/ecs/system.h>
#include <core/graphics/runtime/render_pass.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/packet_runtime.h>
#include <core/task/gpu/persistent_state.h>
#include <core/task/gpu/types.h>


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


struct MeshSkinningRuntimeInstance;

static_assert(SkeletonSkinningMode::LinearBlend == NWB_SKINNED_MESH_SKINNING_MODE_LINEAR_BLEND && SkeletonSkinningMode::DualQuaternion == NWB_SKINNED_MESH_SKINNING_MODE_DUAL_QUATERNION, "Skeleton skinning mode values must match the shader ABI");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshSkinningSystem final
    : public Core::ECS::ISystem
    , public Core::IRenderPass
    , public IRuntimeMeshProvider
{
private:
    using MeshSkinningPushConstants = Impl::MeshSkinningPushConstants;
    static_assert(sizeof(MeshSkinningPushConstants) == NWB_SKINNED_MESH_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning push constants layout must match the shader ABI");
    static_assert(offsetof(MeshSkinningPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_MESHLET_COUNT, "MeshSkinning meshlet-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, skinCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_SKIN_COUNT, "MeshSkinning skin-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, jointCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_JOINT_COUNT, "MeshSkinning joint-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, skinningMode) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_SKINNING_MODE, "MeshSkinning skinning-mode push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, bindlessResourceSlots) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_BINDLESS_RESOURCES_SLOT, "MeshSkinning bindless-resource slot push offset drifted");

    using MeshletBoundsPushConstants = Impl::MeshletBoundsPushConstants;
    static_assert(sizeof(MeshletBoundsPushConstants) == NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning bounds push constants layout must match the shader ABI");
    static_assert(offsetof(MeshletBoundsPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT, "MeshSkinning bounds meshlet-count push offset drifted");
    static_assert(offsetof(MeshletBoundsPushConstants, bindlessResourceSlots) == sizeof(u32) * NWB_SKINNED_MESH_BOUNDS_PUSH_BINDLESS_RESOURCES_SLOT, "MeshSkinning bounds bindless-resource slot push offset drifted");

    using MeshletRepackPushConstants = Impl::MeshletRepackPushConstants;
    static_assert(sizeof(MeshletRepackPushConstants) == NWB_SKINNED_MESH_REPACK_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning repack push constants layout must match the shader ABI");
    static_assert(offsetof(MeshletRepackPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_REPACK_PUSH_MESHLET_COUNT, "MeshSkinning repack meshlet-count push offset drifted");
    static_assert(offsetof(MeshletRepackPushConstants, bindlessResourceSlots) == sizeof(u32) * NWB_SKINNED_MESH_REPACK_PUSH_BINDLESS_RESOURCES_SLOT, "MeshSkinning repack bindless-resource slot push offset drifted");

    struct RuntimeBindlessHeapHandles{
        Core::GpuDescriptorHandle resourceSlots = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle restPosition = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle skinnedPosition = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle restNormal = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle skinnedNormal = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle restTangent = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle skinnedTangent = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle meshletDesc = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle positionRefDeltas = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle attributeRefDeltas = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle attributeSkins = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle skinInfluences = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle jointPalette = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle localVertexRefs = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle primitiveIndices = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle meshletBounds = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle attributeBuffer = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle meshletLocalBounds = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle localBounds = Core::GpuDescriptorHandle::Invalid();
    };

    struct RuntimeResources{
        RuntimeMeshHandle handle;
        MeshSkinningResourceBuffers buffers;
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
        b8 bindlessResourceSlotsUploaded = false;


    public:
        [[nodiscard]] b8 hasPersistentHeapDescriptors(const b8 hasActiveSkin, const b8 hasAttributeBuffer)const noexcept{
            const auto storageHandle = [](const Core::GpuDescriptorHandle handle)noexcept{
                return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
            };
            const auto uniformHandle = [](const Core::GpuDescriptorHandle handle)noexcept{
                return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer;
            };
            const b8 common =
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
                && storageHandle(bindlessHeapHandles.meshletLocalBounds)
                && storageHandle(bindlessHeapHandles.localBounds)
            ;
            return common && (!hasActiveSkin || (storageHandle(bindlessHeapHandles.skinInfluences) && storageHandle(bindlessHeapHandles.jointPalette))) && (!hasAttributeBuffer || storageHandle(bindlessHeapHandles.attributeBuffer));
        }
    };

    using RestToSkinnedCopyByteCounts = MeshSkinningRestToSkinnedCopyByteCounts;

    struct TaskGraphSkinningDeformationTask;
    struct TaskGraphSkinningPostDispatchTask;
    struct TaskGraphSkinningFinalizerTask;
    class FramePlanBuilder;


public:
    using ShaderPathResolveCallback = Function< Expected<Name>(const Name& shaderName, AStringView variantName, const Name& stageName) >;


private:
    [[nodiscard]] static Expected<RestToSkinnedCopyByteCounts> ResolveRestToSkinnedCopyByteCounts(const MeshSkinningRuntimeInstance& instance);


public:
    MeshSkinningSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, IRuntimeMeshRegistry& runtimeMeshRegistry, ShaderPathResolveCallback shaderPathResolver);
    virtual ~MeshSkinningSystem()noexcept override;


public:
    virtual void prepare(Core::ECS::World& world)override;
    [[nodiscard]] virtual Expected<void, Core::CpuTaskAdmissionError::Enum> update(Core::ECS::World& world, f32 delta)noexcept override;
    virtual b8 validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual b8 prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void invalidateResources()override;

    virtual Expected<RuntimeMeshDesc> resolveRuntimeMesh(Core::ECS::EntityID entity)override;
    [[nodiscard]] virtual b8 hasRuntimeMeshBinding(Core::ECS::EntityID entity)const noexcept override;
    virtual void markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests)override;

    void setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept{ m_activeSceneSubsystems = subsystems; }
    [[nodiscard]] b8 isSkinningFeatureEnabled()const noexcept{
        return m_activeSceneSubsystems && m_activeSceneSubsystems->hasSubsystem<SkinningSubsystem>();
    }


private:
    [[nodiscard]] b8 ensureSkinningPipeline();
    [[nodiscard]] b8 ensureBoundsPipeline();
    [[nodiscard]] b8 ensureLocalBoundsPipeline();
    [[nodiscard]] b8 ensureRepackPipeline();
    // Consume-only pipeline checks for the per-frame prepare path; creation belongs to validateResources via the ensure* setup above.
    [[nodiscard]] b8 skinningPipelineReady()const noexcept;
    [[nodiscard]] b8 boundsPipelinesReady()const noexcept;
    [[nodiscard]] b8 repackPipelineReady()const noexcept;
    // Declares frame-local skinning as one Graphics packet; accepted task commits dirty state.
    [[nodiscard]] b8 submitFrameSkinningGraph();
    [[nodiscard]] b8 prepareRuntimeMeshResources(MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 recordGraphOwnedSkinningDeformation(const MeshSkinningGraphDispatchPlan& plan, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
    [[nodiscard]] b8 recordGraphOwnedSkinningPostDispatch(const MeshSkinningGraphDispatchPlan& plan, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
    void confirmGraphOwnedSkinningDispatch(const MeshSkinningGraphDispatchPlan& plan)noexcept;
    void collectLiveSkinningStateBuffers(Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outBuffers, Core::Alloc::ScratchArena& scratchArena)const;
    [[nodiscard]] b8 replaceAcceptedSkinningState(const Core::CommandListResourceStateHandoff& state, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Expected<b8> ensureRuntimeResources(MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 createRuntimeResourceBindlessHeapHandles(MeshSkinningRuntimeInstance& instance, RuntimeResources& resources);
    void releaseRuntimeResourceBindlessHeapHandles(RuntimeResources& resources);
    void pruneRuntimeResources();
    void resolveFrameTaskGraphAttempt(const Core::GpuCompiledGraph::ReadView& planAccess)noexcept;
    // Resets reused frame graph storage so the next render() redeclares into clean members.
    void resetFrameTaskGraph();


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    IRuntimeMeshRegistry& m_runtimeMeshRegistry;
    ShaderPathResolveCallback m_shaderPathResolver;
    MeshSkinningRuntimeCache m_runtimeMeshCache;
    ActiveSceneSubsystems m_defaultSceneSubsystems;
    const ActiveSceneSubsystems* m_activeSceneSubsystems = nullptr;
    b8 m_runtimeSetupReady = false;

    HashMap<u64, RuntimeResources, Core::Alloc::GlobalArena, Hasher<u64>, EqualTo<u64>> m_runtimeResources;
    // Reused across submitFrameSkinningGraph() calls so per-frame render performs no GlobalArena creation.
    Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena> m_frameDispatchPlans;
    Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena> m_frameDeformationPlans;
    Vector<MeshSkinningLocalBoundsDispatch, Core::Alloc::GlobalArena> m_frameLocalBoundsDispatches;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> m_frameLiveBuffers;
    // Compiler artifacts. Declared once, reset per submit; render() only redeclares into reused storage.
    Core::GpuTaskGraph m_frameTaskGraph;
    Core::GpuTaskGraphAnalysis m_frameTaskGraphAnalysis;
    Core::GpuTaskGraphQueueAssignments m_frameTaskGraphQueueAssignments;
    Core::GpuCompiledGraph m_frameCompiledGraph;
    Core::GpuRecordedGraph m_frameRecordedGraph;
    Core::GpuGraphSubmissionTransaction m_frameSubmissionTransaction;
    // The graph-runtime cache retains only accepted live skinning resources between graph generations.
    Core::GpuPersistentResourceStateCache m_acceptedSkinningState;
    Core::BindingLayoutHandle m_skinningBindingLayout;
    Core::ShaderHandle m_skinningComputeShader;
    Core::ComputePipelineHandle m_skinningComputePipeline;
    Core::BindingLayoutHandle m_boundsBindingLayout;
    Core::ShaderHandle m_boundsComputeShader;
    Core::ComputePipelineHandle m_boundsComputePipeline;
    Core::ShaderHandle m_localBoundsComputeShader;
    Core::ComputePipelineHandle m_localBoundsComputePipeline;
    Core::BindingLayoutHandle m_repackBindingLayout;
    Core::ShaderHandle m_repackComputeShader;
    Core::ComputePipelineHandle m_repackComputePipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

