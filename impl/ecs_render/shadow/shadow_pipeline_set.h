// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shadow/rt_shadow_state.h>
#include <core/assets/ref.h>


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
class ComputeShader;
class ShadowSubsystem;


class ShadowPipelineSet final : NoCopy{
public:
    ShadowPipelineSet(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, RendererShaderSystem& shaderSystem, RtShadowState& shadow, RtSoftShadowState& softShadow)noexcept
        : m_arena(arena)
        , m_graphics(graphics)
        , m_shaderSystem(shaderSystem)
        , m_shadow(shadow)
        , m_softShadow(softShadow)
    {}


public:
    [[nodiscard]] b8 ensureShadowPipeline();
    [[nodiscard]] b8 ensureShadowSoftPipeline();
    [[nodiscard]] b8 ensureSwShadowPipeline();
    [[nodiscard]] b8 ensureSoftShadowResolvePipeline();
    [[nodiscard]] b8 ensureShadowGeometryDownsamplePipeline();
    [[nodiscard]] b8 ensureSoftTransparentResolvePipeline();
    [[nodiscard]] b8 ensureShadowReprojectMergePipeline();
    void prepareSoftCombinedResolvePipelines();


private:
    [[nodiscard]] b8 ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::* shaderMember, NameText<char> debugLabel);
    [[nodiscard]] b8 ensureSoftwareTransparentSamplingPipeline();
    [[nodiscard]] b8 ensureSoftCombinedUpsamplePipeline();
    [[nodiscard]] b8 ensureSoftCombinedWaveletPipeline();
    [[nodiscard]] b8 ensureSoftCombinedTemporalPipeline();


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    RendererShaderSystem& m_shaderSystem;
    RtShadowState& m_shadow;
    RtSoftShadowState& m_softShadow;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

