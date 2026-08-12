// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_cache.h"
#include "submission_state.h"

#include <core/ecs/system.h>
#include <core/graphics/render_pass.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/task_graph/types.h>
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
struct MeshSkinningInfluenceGpu;

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
        // UniformBuffer heap slot for RuntimeBindlessResourceSlots.
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
        // UniformBuffer heap slot for RuntimeBindlessResourceSlots.
        u32 bindlessResourceSlots = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
    };
    static_assert(sizeof(MeshletBoundsPushConstants) == NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning bounds push constants layout must match the shader ABI");
    static_assert(offsetof(MeshletBoundsPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT, "MeshSkinning bounds meshlet-count push offset drifted");
    static_assert(offsetof(MeshletBoundsPushConstants, bindlessResourceSlots) == sizeof(u32) * NWB_SKINNED_MESH_BOUNDS_PUSH_BINDLESS_RESOURCES_SLOT, "MeshSkinning bounds bindless-resource slot push offset drifted");

    struct MeshletRepackPushConstants{
        u32 meshletCount = 0;
        // UniformBuffer heap slot for RuntimeBindlessResourceSlots.
        u32 bindlessResourceSlots = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
    };
    static_assert(sizeof(MeshletRepackPushConstants) == NWB_SKINNED_MESH_REPACK_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning repack push constants layout must match the shader ABI");
    static_assert(offsetof(MeshletRepackPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_REPACK_PUSH_MESHLET_COUNT, "MeshSkinning repack meshlet-count push offset drifted");
    static_assert(offsetof(MeshletRepackPushConstants, bindlessResourceSlots) == sizeof(u32) * NWB_SKINNED_MESH_REPACK_PUSH_BINDLESS_RESOURCES_SLOT, "MeshSkinning repack bindless-resource slot push offset drifted");

    // Mirrors NwbSkinnedMeshBindlessResources exactly: four std140 uint4 lanes. Every handle below is a persistent
    // StorageBuffer-heap registration. The selector payload itself is a UniformBuffer heap entry.
    struct RuntimeBindlessResourceSlots{
        u32 restPosition = 0u;
        u32 skinnedPosition = 0u;
        u32 restNormal = 0u;
        u32 skinnedNormal = 0u;

        u32 restTangent = 0u;
        u32 skinnedTangent = 0u;
        u32 meshletDesc = 0u;
        u32 positionRefDeltas = 0u;

        u32 attributeRefDeltas = 0u;
        u32 attributeSkins = 0u;
        u32 skinInfluences = 0u;
        u32 jointPalette = 0u;

        u32 localVertexRefs = 0u;
        u32 primitiveIndices = 0u;
        u32 meshletBounds = 0u;
        u32 attributeBuffer = 0u;

        [[nodiscard]] constexpr bool operator==(const RuntimeBindlessResourceSlots&)const = default;
    };
    static_assert(sizeof(RuntimeBindlessResourceSlots) == sizeof(u32) * 16u, "MeshSkinning bindless resource slots must stay four uint4 lanes");

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
        RuntimeBindlessResourceSlots bindlessResourceSlots;
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

    struct RuntimePayloadViews{
        const MeshSkinningInfluenceGpu* skinInfluences = nullptr;
        const SkeletonJointMatrix* jointPalette = nullptr;
        usize skinInfluenceCount = 0;
        usize jointPaletteCount = 0;

        [[nodiscard]] bool hasActiveSkin()const{ return skinInfluenceCount != 0u && jointPaletteCount != 0u; }
    };

    // The graph-owned no-active-skin copy is accepted before the legacy bounds command list opens.  Retain the
    // exact buffer generation so that recording can only skip its native duplicate when the runtime mesh has not
    // changed between graph declaration and dispatch.
    struct GraphOwnedRestCopyPlan{
        RuntimeMeshHandle handle;
        u32 editRevision = 0u;
        Core::BufferHandle restPositionBuffer;
        Core::BufferHandle restNormalBuffer;
        Core::BufferHandle restTangentBuffer;
        Core::BufferHandle skinnedPositionBuffer;
        Core::BufferHandle skinnedNormalBuffer;
        Core::BufferHandle skinnedTangentBuffer;
        usize positionBytes = 0u;
        usize normalBytes = 0u;
        usize tangentBytes = 0u;
    };

    // The graph task retains only immutable per-mesh dispatch inputs. It resolves its imported buffers and pipelines
    // from graph-owned IDs while recording, then publishes the dirty-state and selector-residency commit only after
    // the containing primary-Graphics packet is accepted.
    struct GraphOwnedSkinningDispatchPlan{
        RuntimeMeshHandle handle;
        MeshSkinningSubmissionCommit submissionCommit;
        bool hasActiveSkin = false;
        bool copiedRestStreams = false;
        bool updatesMeshletBounds = false;
        bool repacksNormals = false;
        u32 meshletCount = 0u;
        u32 skinCount = 0u;
        u32 jointCount = 0u;
        u32 skinningMode = SkeletonSkinningMode::LinearBlend;
        u32 attributeCount = 0u;
        u32 bindlessResourceSlots = 0u;

        // Acceptance validates this exact selector generation before setting its residency bit.
        Core::BufferHandle bindlessResourceSlotsBuffer;
        Core::GpuDescriptorHandle bindlessResourceSlotsDescriptor = Core::GpuDescriptorHandle::invalid();
        RuntimeBindlessResourceSlots bindlessResourceSlotsPayload;

        Core::GpuGraphResourceId bindlessResourceSlotsResource;
        Core::GpuGraphResourceId restPositionResource;
        Core::GpuGraphResourceId restNormalResource;
        Core::GpuGraphResourceId restTangentResource;
        Core::GpuGraphResourceId skinnedPositionResource;
        Core::GpuGraphResourceId skinnedNormalResource;
        Core::GpuGraphResourceId skinnedTangentResource;
        Core::GpuGraphResourceId meshletDescResource;
        Core::GpuGraphResourceId meshletBoundsResource;
        Core::GpuGraphResourceId meshletPositionRefDeltaResource;
        Core::GpuGraphResourceId meshletAttributeRefDeltaResource;
        Core::GpuGraphResourceId meshletLocalVertexRefResource;
        Core::GpuGraphResourceId meshletPrimitiveIndexResource;
        Core::GpuGraphResourceId attributeSkinResource;
        Core::GpuGraphResourceId skinResource;
        Core::GpuGraphResourceId jointPaletteResource;
        Core::GpuGraphResourceId attributeResource;
        Core::GpuGraphPipelineId skinningPipeline;
        Core::GpuGraphPipelineId boundsPipeline;
        Core::GpuGraphPipelineId repackPipeline;
    };

    struct TaskGraphSkinningDispatchTask;
    struct TaskGraphSkinningFinalizerTask;


public:
    using ShaderPathResolveCallback = Function<
        bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)
    >;


public:
    MeshSkinningSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
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
    virtual bool containsRuntimeMesh(const Name& meshKey, u64 version)override;

private:
    [[nodiscard]] bool ensureSkinningPipeline();
    [[nodiscard]] bool ensureBoundsPipeline();
    [[nodiscard]] bool ensureRepackPipeline();
    [[nodiscard]] bool dispatchRuntimeMesh(
        Core::CommandList& commandList,
        MeshSkinningRuntimeInstance& instance,
        const SkeletonJointPaletteComponent* jointPalette,
        const SkeletonPoseComponent* skeletonPose,
        MeshSkinningSubmissionCommit& outCommit
    );
    // Declares all frame-local skinning work as one graph-owned primary-Graphics packet: immutable palette/selector
    // uploads and rest-stream copies feed the compute continuation, whose accepted task commits dirty-state changes.
    [[nodiscard]] bool submitFrameSkinningGraph();
    [[nodiscard]] bool prepareRuntimeMeshResources(
        MeshSkinningRuntimeInstance& instance,
        const SkeletonJointPaletteComponent* jointPalette,
        const SkeletonPoseComponent* skeletonPose
    );
    [[nodiscard]] bool copyRestToSkinned(Core::CommandList& commandList, MeshSkinningRuntimeInstance& instance);
    [[nodiscard]] bool hasGraphOwnedRestCopyPlan(const MeshSkinningRuntimeInstance& instance)const;
    [[nodiscard]] bool recordGraphOwnedSkinningDispatch(
        const GraphOwnedSkinningDispatchPlan& plan,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
    void confirmGraphOwnedSkinningDispatch(const GraphOwnedSkinningDispatchPlan& plan)noexcept;
    void transitionGraphCopiedRestStreams(Core::CommandList& commandList, MeshSkinningRuntimeInstance& instance)const;
    [[nodiscard]] static bool resolveRestToSkinnedCopyByteCounts(
        const MeshSkinningRuntimeInstance& instance,
        usize& outPositionBytes,
        usize& outNormalBytes,
        usize& outTangentBytes
    );
    void resetAcceptedSkinningStateHandoff()noexcept;
    [[nodiscard]] bool replaceAcceptedSkinningStateHandoff(const Core::CommandListResourceStateHandoff& state);
    [[nodiscard]] bool dispatchMeshletBounds(
        Core::CommandList& commandList,
        MeshSkinningRuntimeInstance& instance,
        const RuntimeResources& resources
    );
    [[nodiscard]] bool dispatchRepackNormals(
        Core::CommandList& commandList,
        MeshSkinningRuntimeInstance& instance,
        const RuntimeResources& resources
    );
    [[nodiscard]] bool ensureRuntimeResources(
        MeshSkinningRuntimeInstance& instance,
        const RuntimePayloadViews& payloadViews,
        RuntimeResources*& outResources,
        bool& outResourcesRebuilt
    );
    [[nodiscard]] bool createRuntimeResourceBindlessHeapHandles(MeshSkinningRuntimeInstance& instance, RuntimeResources& resources);
    [[nodiscard]] bool uploadRuntimeResourceBindlessSlots(
        Core::CommandList& commandList,
        RuntimeResources& resources,
        bool& outUploadRecorded
    );
    void releaseRuntimeResourceBindlessHeapHandles(RuntimeResources& resources);
    void pruneRuntimeResources();


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    IRuntimeMeshRegistry& m_runtimeMeshRegistry;
    ShaderPathResolveCallback m_shaderPathResolver;
    MeshSkinningRuntimeCache m_runtimeMeshCache;

    HashMap<u64, RuntimeResources, Hasher<u64>, EqualTo<u64>, Core::Alloc::GlobalArena> m_runtimeResources;
    Vector<GraphOwnedRestCopyPlan, Core::Alloc::GlobalArena> m_graphOwnedRestCopyPlans;
    // The accepted graph state persists across frames and seeds the next graph packet with live resource states.
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> m_acceptedSkinningStateBuffers;
    Core::CommandListResourceStateHandoff m_acceptedSkinningStateHandoff;
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

