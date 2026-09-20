// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "optical_scene.h"

#include <impl/assets/graphics/raytrace/optical_bounds_finalize_constants.h>

#include <core/alloc/global.h>
#include <core/graphics/rhi/command.h>
#include <global/arena_object.h>
#include <global/refcount_ptr.h>


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

struct RayTracingOpticalRuntimeInputGpu{
    Float34U objectToWorld = {};
    u32 localBoundsSlot = Limit<u32>::s_Max;
    u32 instanceIndex = 0u;
    u32 reserved[2] = {};
};
static_assert(sizeof(RayTracingOpticalRuntimeInputGpu) == NWB_OPTICAL_BOUNDS_RUNTIME_INPUT_BYTES);
static_assert(offsetof(RayTracingOpticalRuntimeInputGpu, localBoundsSlot) == NWB_OPTICAL_BOUNDS_RUNTIME_SLOT_OFFSET);
static_assert(offsetof(RayTracingOpticalRuntimeInputGpu, instanceIndex) == NWB_OPTICAL_BOUNDS_RUNTIME_INSTANCE_OFFSET);

// Immutable frame inputs retain the same accepted bounds and transforms as the emitted scene instance order.
struct RayTracingOpticalBoundsFinalizeSnapshot{
    Core::BufferHandle inputBuffer;
    Core::BufferHandle outputBuffer;
    Core::ComputePipelineHandle pipeline;
    Vector<RayTracingOpticalRuntimeInputGpu, Core::Alloc::GlobalArena> inputs;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> boundsBuffers;
    Core::GraphicsRuntime& graphics;
    Core::GpuDescriptorHandle inputDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle outputDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle sourceDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuPhysicalQueueId queue;
    u32 instanceCount = 0u;
    bool staticBoundsComplete = false;

    RayTracingOpticalBoundsFinalizeSnapshot(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics);
};
using RayTracingOpticalBoundsFinalizeControl = RefCounter<RayTracingOpticalBoundsFinalizeSnapshot>;
using RayTracingOpticalBoundsFinalizeHandle = RefCountPtr<RayTracingOpticalBoundsFinalizeControl, ArenaRefDeleter<RayTracingOpticalBoundsFinalizeControl, Core::Alloc::GlobalArena>>;

class RayTracingOpticalBoundsFinalizeResources final : NoCopy{
public:
    RayTracingOpticalBoundsFinalizeResources(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics);


public:
    void invalidate();
    [[nodiscard]] RayTracingOpticalBoundsFinalizeHandle prepare(
        const RayTracingOpticalSceneGather& gather,
        Core::GpuDescriptorHandle sourceDescriptor,
        RendererShaderSystem& shaderSystem
    );

private:
    [[nodiscard]] bool ensurePipeline(RendererShaderSystem& shaderSystem);

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    Core::BufferHandle m_inputBuffer;
    Core::BufferHandle m_outputBuffer;
    Core::GpuDescriptorHandle m_inputDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_outputDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::BindingLayoutHandle m_bindingLayout;
    Core::ShaderHandle m_shader;
    Core::ComputePipelineHandle m_pipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

