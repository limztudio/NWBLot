// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;
namespace Alloc{
    class GlobalArena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererShaderSystem;
class RayTraceMaterialResources;


class SoftwareBvhBuilder final : NoCopy{
public:
    SoftwareBvhBuilder(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, RendererShaderSystem& shaderSystem, RtSceneBvhState& state)noexcept
        : m_arena(arena)
        , m_graphics(graphics)
        , m_shaderSystem(shaderSystem)
        , m_state(state)
    {}


public:
    void releaseSwBvhScratchHeapHandles();
    [[nodiscard]] b8 ensureSceneBvhBuffers(u32 instanceCount, RayTraceMaterialResources& materials);
    [[nodiscard]] b8 ensureMeshSwBvhResources(u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] b8 meshSwBvhResourcesReady(const Core::BufferHandle& nodeBuffer, const Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle)noexcept;
    [[nodiscard]] b8 buildMeshSwBvhPrepared(
        u32 positionHeapSlot,
        u32 triangleIndexHeapSlot,
        u32 primitiveCount,
        const Float3Int& aabbMin,
        const Float3Int& aabbMax,
        Core::GpuDescriptorHandle nodeHeapHandle,
        Core::GpuDescriptorHandle parentHeapHandle,
        Core::CommandList& commandList,
        Core::BufferHandle& nodeBuffer,
        Core::BufferHandle& parentBuffer
    );
    [[nodiscard]] b8 refitMeshSwBvhPrepared(
        u32 positionHeapSlot,
        u32 triangleIndexHeapSlot,
        u32 primitiveCount,
        Core::GpuDescriptorHandle nodeHeapHandle,
        Core::GpuDescriptorHandle parentHeapHandle,
        Core::CommandList& commandList,
        Core::BufferHandle& nodeBuffer,
        Core::BufferHandle& parentBuffer
    );


private:
    [[nodiscard]] b8 ensureBvhSortPipeline();
    [[nodiscard]] b8 ensureBvhSortBuffers(usize paddedCount);
    [[nodiscard]] b8 bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount);
    [[nodiscard]] b8 ensureBvhBuildPipeline();
    [[nodiscard]] b8 ensureBvhVisitCounterBuffer(usize primitiveCount);
    [[nodiscard]] b8 createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    RendererShaderSystem& m_shaderSystem;
    RtSceneBvhState& m_state;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

