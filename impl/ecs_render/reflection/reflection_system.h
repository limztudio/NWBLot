// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "settings.h"
#include "statistics_readback.h"
#include "postprocess_resources.h"
#include "feedback_resources.h"

#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/ecs_render/raytrace/scene_resources.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/alloc/global.h>
#include <core/graphics/rhi/pipeline.h>


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
struct DeferredFrameTargets;

struct ReflectionFrameParameters{
#define NWB_REFLECTION_CPU_UINT_FIELD(name, value) u32 name = value;
    NWB_REFLECTION_FRAME_UINT_FIELDS(NWB_REFLECTION_CPU_UINT_FIELD)
#undef NWB_REFLECTION_CPU_UINT_FIELD
#define NWB_REFLECTION_CPU_FLOAT_FIELD(name, value) f32 name = value;
    NWB_REFLECTION_FRAME_FLOAT_FIELDS(NWB_REFLECTION_CPU_FLOAT_FIELD)
#undef NWB_REFLECTION_CPU_FLOAT_FIELD
};
static_assert(sizeof(ReflectionFrameParameters) == 192u);

struct ReflectionDepthPyramidMip{
    Name taskIdentity = NAME_NONE;
    u32 sampledSlot = 0u;
    u32 storageSlot = 0u;
    u32 width = 0u;
    u32 height = 0u;
};

struct ReflectionDepthPyramidSnapshot{
    // A native mip chain for u32 extents cannot contain more than 32 levels.
    static constexpr u32 s_MaxMipCount = NWB_REFLECTION_MAX_DEPTH_MIPS;

    Core::TextureHandle texture;
    Core::ComputePipelineHandle pipeline;
    ReflectionDepthPyramidMip mips[s_MaxMipCount];
    u32 mipCount = 0u;
    u32 sampledSlot = 0u;
    u32 sourceDepthSlot = 0u;

    [[nodiscard]] bool valid()const noexcept{
        return texture && pipeline && mipCount > 0u && mipCount <= s_MaxMipCount;
    }
};

// Resource handles and descriptor slots are copied together before graph declaration. Only the parameter value
// varies per frame; no recording callback consults the live resource owner or creates descriptors/pipelines.
struct ReflectionFrameSnapshot{
    ReflectionFrameParameters parameters;
    Core::TextureHandle opaqueRadiance;
    Core::TextureHandle glassRadiance;
    Core::BufferHandle queue;
    Core::BufferHandle counters;
    Core::BufferHandle indirectArgs;
    Core::BufferHandle frameParameters;
    Core::ComputePipelineHandle classifyPipeline;
    Core::ComputePipelineHandle buildArgsPipeline;
    Core::ComputePipelineHandle hardwarePipeline;
    ReflectionDepthPyramidSnapshot depthPyramid;
    RayTracingSceneGraphResources scene;
    ReflectionStatisticsReadbackSnapshot statistics;
    ReflectionPostprocessSnapshot postprocess;
    ReflectionFeedbackSnapshot feedback;
    u32 frameParametersSlot = 0u;

    [[nodiscard]] bool valid()const noexcept{
        return
            parameters.width > 0u && parameters.height > 0u && opaqueRadiance && glassRadiance
            && queue && counters && indirectArgs && frameParameters && classifyPipeline && buildArgsPipeline
            && depthPyramid.valid()
        ;
    }
};

class RendererReflectionSystem final : NoCopy{
public:
    RendererReflectionSystem(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, RendererShaderSystem& shaders);

    // The caller joins submitted work and discards pending graph snapshots before invalidation/device teardown.
    void invalidateResources();
    [[nodiscard]] bool prepareResources(u32 width, u32 height, bool prepareHardware, const ReflectionSettings& settings);
    void pollStatistics();
    [[nodiscard]] bool tryGetLatestStatistics(ReflectionStatistics& outStatistics)const;
    [[nodiscard]] ReflectionFrameSnapshot snapshotFrameResources(
        const DeferredFrameTargets& targets,
        const ECSRenderDetail::MeshViewBufferSnapshot& view,
        const RayTracingSceneGraphResources& scene,
        const ReflectionSettings& settings,
        u32 frameIndex,
        const ReflectionSceneContentStamp& stamp
    )const;

private:
    void releaseTargets();
    [[nodiscard]] bool prepareQueue(u32 capacity);
    [[nodiscard]] bool preparePipelines(bool prepareHardware);

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    RendererShaderSystem& m_shaders;
    ReflectionStatisticsReadback m_statistics;
    RendererReflectionPostprocess m_postprocess;
    RendererReflectionFeedback m_feedback;
    ReflectionFrameSnapshot m_resources;
    Core::BindingLayoutHandle m_bindingLayout;
    Core::BindingLayoutHandle m_depthBindingLayout;
    Core::ShaderHandle m_classifyShader;
    Core::ShaderHandle m_buildArgsShader;
    Core::ShaderHandle m_hardwareShader;
    Core::ShaderHandle m_plainHardwareShader;
    Core::ComputePipelineHandle m_plainHardwarePipeline;
    Core::ShaderHandle m_depthShader;
    Core::GpuDescriptorHandle m_descriptors[8];
    Core::GpuDescriptorHandle m_depthSampledDescriptor;
    Core::GpuDescriptorHandle m_depthMipSampledDescriptors[ReflectionDepthPyramidSnapshot::s_MaxMipCount];
    Core::GpuDescriptorHandle m_depthMipStorageDescriptors[ReflectionDepthPyramidSnapshot::s_MaxMipCount];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

