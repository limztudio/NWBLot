// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/assets/graphics/bvh/scene_refit_constants.h>

#include <core/alloc/global.h>
#include <core/graphics/rhi/command.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <global/arena_object.h>
#include <global/refcount_ptr.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererShaderSystem;

struct SoftwareSceneRefitInstanceGpu{
    Float34U objectToWorld = {};
    u32 meshNodeSlot = Limit<u32>::s_Max;
    u32 reserved[3] = {};
};
static_assert(sizeof(SoftwareSceneRefitInstanceGpu) == NWB_SCENE_BVH_REFIT_INSTANCE_BYTES);
static_assert(offsetof(SoftwareSceneRefitInstanceGpu, meshNodeSlot) == NWB_SCENE_BVH_REFIT_MESH_SLOT_OFFSET);

// Frozen instance order and owning roots are consumed only after their prepared mesh builds.
struct SoftwareSceneRefitSnapshot{
    Core::BufferHandle inputBuffer;
    Core::BufferHandle sceneNodes;
    Core::ComputePipelineHandle pipeline;
    Vector<SoftwareSceneRefitInstanceGpu, Core::Alloc::GlobalArena> inputs;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> meshNodes;
    Core::GraphicsRuntime& graphics;
    Core::GpuDescriptorHandle inputDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle sceneDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuPhysicalQueueId queue;
    u32 nodeCount = 0u;

    SoftwareSceneRefitSnapshot(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics);
};
using SoftwareSceneRefitControl = RefCounter<SoftwareSceneRefitSnapshot>;
using SoftwareSceneRefitHandle = RefCountPtr<SoftwareSceneRefitControl, ArenaRefDeleter<SoftwareSceneRefitControl, Core::Alloc::GlobalArena>>;

class SoftwareSceneRefitResources final : NoCopy{
public:
    SoftwareSceneRefitResources(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics);

public:
    void invalidate();
    [[nodiscard]] SoftwareSceneRefitHandle prepare(
        const SoftwareSceneRefitInstanceGpu* inputs, const Core::BufferHandle* meshNodes, usize instanceCount,
        const Core::BufferHandle& sceneNodes, Core::GpuDescriptorHandle sceneDescriptor, u32 nodeCount,
        RendererShaderSystem& shaderSystem
    );

private:
    [[nodiscard]] bool ensurePipeline(RendererShaderSystem& shaderSystem);

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    Core::BufferHandle m_inputBuffer;
    Core::GpuDescriptorHandle m_inputDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::BindingLayoutHandle m_bindingLayout;
    Core::ShaderHandle m_shader;
    Core::ComputePipelineHandle m_pipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

