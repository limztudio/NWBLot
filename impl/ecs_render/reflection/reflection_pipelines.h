// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/alloc/global.h>
#include <core/graphics/rhi/pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN
class GraphicsRuntime;
NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererShaderSystem;
struct ReflectionFrameSnapshot;


class RendererReflectionPipelines final : NoCopy{
public:
    RendererReflectionPipelines(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, RendererShaderSystem& shaders)noexcept
        : m_arena(arena)
        , m_graphics(graphics)
        , m_shaders(shaders)
    {}


public:
    [[nodiscard]] b8 prepare(b8 prepareHardware, b8 prepareCsgHardware, ReflectionFrameSnapshot& resources);
    void invalidate(ReflectionFrameSnapshot& resources);
    [[nodiscard]] const Core::ComputePipelineHandle& plainHardwarePipeline()const noexcept{ return m_plainHardwarePipeline; }
    [[nodiscard]] const Core::ComputePipelineHandle& unspecifiedHardwarePipeline()const noexcept{ return m_unspecifiedHardwarePipeline; }
    [[nodiscard]] const Core::ComputePipelineHandle& csgBuildArgsPipeline()const noexcept{ return m_csgBuildArgsPipeline; }
    [[nodiscard]] const Core::ComputePipelineHandle& csgHardwarePipeline()const noexcept{ return m_csgHardwarePipeline; }
    [[nodiscard]] const Core::ComputePipelineHandle& csgPlainHardwarePipeline()const noexcept{ return m_csgPlainHardwarePipeline; }
    [[nodiscard]] const Core::ComputePipelineHandle& csgUnspecifiedHardwarePipeline()const noexcept{ return m_csgUnspecifiedHardwarePipeline; }


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    RendererShaderSystem& m_shaders;
    Core::BindingLayoutHandle m_bindingLayout;
    Core::BindingLayoutHandle m_depthBindingLayout;
    Core::BindingLayoutHandle m_csgBindingLayout;
    Core::ShaderHandle m_classifyShader;
    Core::ShaderHandle m_buildArgsShader;
    Core::ShaderHandle m_hardwareShader;
    Core::ShaderHandle m_plainHardwareShader;
    Core::ShaderHandle m_unspecifiedHardwareShader;
    Core::ComputePipelineHandle m_plainHardwarePipeline;
    Core::ComputePipelineHandle m_unspecifiedHardwarePipeline;
    Core::ShaderHandle m_csgBuildArgsShader;
    Core::ComputePipelineHandle m_csgBuildArgsPipeline;
    Core::ShaderHandle m_csgHardwareShader;
    Core::ShaderHandle m_csgPlainHardwareShader;
    Core::ShaderHandle m_csgUnspecifiedHardwareShader;
    Core::ComputePipelineHandle m_csgHardwarePipeline;
    Core::ComputePipelineHandle m_csgPlainHardwarePipeline;
    Core::ComputePipelineHandle m_csgUnspecifiedHardwarePipeline;
    Core::ShaderHandle m_depthShader;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

