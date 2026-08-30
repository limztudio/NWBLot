// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/pipeline.h>
#include <core/graphics/rhi/pipeline_state.h>

#include <impl/assets/graphics/scene/binding_slots.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDeferredSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDeferredState final : NoCopy{
    friend class RendererDeferredSystem;

public:
    RendererDeferredState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_lightingBindingLayout;
    Core::BufferHandle m_sceneShadingBuffer;
    Core::BufferHandle m_lightBuffer;
    Core::ShaderHandle m_lightingComputeShader;
    Core::ComputePipelineHandle m_lightingPipeline;
    Core::BindingLayoutHandle m_compositeComputeBindingLayout;
    Core::ShaderHandle m_compositeComputeShader;
    Core::ComputePipelineHandle m_compositeComputePipeline;
    Core::BindingLayoutHandle m_presentBindingLayout;
    Core::SamplerHandle m_sampler;
    Core::ShaderHandle m_presentPixelShader;
    Core::GraphicsPipelineHandle m_presentPipeline;
    u8 m_sceneShadingGpuData[sizeof(f32) * NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT] = {};
    bool m_sceneShadingGpuDataValid = false;
    // Cached bytes avoid redundant light-buffer uploads.
    u8 m_lightGpuData[sizeof(f32) * NWB_SCENE_LIGHT_RECORD_FLOAT_COUNT * NWB_SCENE_MAX_LIGHTS] = {};
    u32 m_lightGpuDataCount = 0u;
    bool m_lightGpuDataValid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

