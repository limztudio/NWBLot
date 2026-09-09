// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "history.h"

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN
class GraphicsRuntime;
NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererShaderSystem;

struct ReflectionRadianceBinding{
    Core::TextureHandle texture;
    u32 sampledSlot = 0u;
    u32 storageSlot = 0u;
};

struct ReflectionPostprocessSnapshot{
    ReflectionHistoryControlHandle control;
    ReflectionHistoryPlan history;
    ReflectionRadianceBinding current;
    ReflectionRadianceBinding previous;
    ReflectionRadianceBinding spatial;
    Core::ComputePipelineHandle temporalPipeline;
    Core::ComputePipelineHandle spatialPipeline;
    u32 deferredResourcesSlot = 0u;
    u32 opaqueSpecularSlot = 0u;
    u32 viewSlot = 0u;
    bool spatialEnabled = false;
};

// Only the extra opaque history bank and spatial destination live here. Glass stays on its smooth transport path.
class RendererReflectionPostprocess final : NoCopy{
public:
    RendererReflectionPostprocess(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, RendererShaderSystem& shaders);
    void invalidateResources();
    [[nodiscard]] bool prepareResources(u32 width, u32 height, const ReflectionSettings& settings);
    [[nodiscard]] ReflectionPostprocessSnapshot snapshot(
        const ReflectionRadianceBinding& base,
        const ReflectionSceneContentStamp& stamp,
        const ReflectionSettings& settings,
        u64 graphicsFrameIndex
    )const;

private:
    void releaseTargets();
    [[nodiscard]] bool preparePipeline(Core::ComputePipelineHandle& pipeline, Core::ShaderHandle& shader, Name name);
    [[nodiscard]] bool prepareImage(ReflectionRadianceBinding& image, u32 descriptorIndex, Name name);

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    RendererShaderSystem& m_shaders;
    ReflectionHistoryControlHandle m_control;
    ReflectionRadianceBinding m_history;
    ReflectionRadianceBinding m_spatial;
    Core::BindingLayoutHandle m_layout;
    Core::ComputePipelineHandle m_temporalPipeline;
    Core::ComputePipelineHandle m_spatialPipeline;
    Core::ShaderHandle m_temporalShader;
    Core::ShaderHandle m_spatialShader;
    Core::GpuDescriptorHandle m_descriptors[4];
    u32 m_width = 0u;
    u32 m_height = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

