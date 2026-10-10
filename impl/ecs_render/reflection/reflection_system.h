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
#include <core/graphics/gpu_timing.h>


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

inline constexpr StringView s_ReflectionHwOpticalVariant = "NWB_REFLECTION_OPTICAL_TRANSPORT=1;NWB_RT_CSG_ENABLED=0";
inline constexpr StringView s_ReflectionHwPlainVariant = "NWB_REFLECTION_OPTICAL_TRANSPORT=0;NWB_RT_CSG_ENABLED=0";
inline constexpr StringView s_ReflectionHwUnspecifiedVariant = "NWB_REFLECTION_OPTICAL_TRANSPORT=2;NWB_RT_CSG_ENABLED=0";
inline constexpr StringView s_ReflectionBuildArgsVariant = "NWB_RT_CSG_ENABLED=0";
inline constexpr StringView s_ReflectionCsgBuildArgsVariant = "NWB_RT_CSG_ENABLED=1";
inline constexpr StringView s_ReflectionHwCsgOpticalVariant = "NWB_REFLECTION_OPTICAL_TRANSPORT=1;NWB_RT_CSG_ENABLED=1";
inline constexpr StringView s_ReflectionHwCsgPlainVariant = "NWB_REFLECTION_OPTICAL_TRANSPORT=0;NWB_RT_CSG_ENABLED=1";
inline constexpr StringView s_ReflectionHwCsgUnspecifiedVariant = "NWB_REFLECTION_OPTICAL_TRANSPORT=2;NWB_RT_CSG_ENABLED=1";

struct ReflectionFrameParameters{
#define NWB_REFLECTION_CPU_UINT_FIELD(name, value) u32 name = value;
    NWB_REFLECTION_FRAME_UINT_FIELDS(NWB_REFLECTION_CPU_UINT_FIELD)
#undef NWB_REFLECTION_CPU_UINT_FIELD
#define NWB_REFLECTION_CPU_FLOAT_FIELD(name, value) f32 name = value;
    NWB_REFLECTION_FRAME_FLOAT_FIELDS(NWB_REFLECTION_CPU_FLOAT_FIELD)
#undef NWB_REFLECTION_CPU_FLOAT_FIELD
};
static_assert(sizeof(ReflectionFrameParameters) == 176u);
static_assert(offsetof(ReflectionFrameParameters, opaqueSpecularSlot) == 16u);
static_assert(offsetof(ReflectionFrameParameters, maxRayDistance) == 128u);
static_assert(offsetof(ReflectionFrameParameters, environmentTopR) == 140u);
static_assert(offsetof(ReflectionFrameParameters, environmentBottomR) == 152u);
static_assert(offsetof(ReflectionFrameParameters, screenThickness) == 164u);

struct ReflectionDepthPyramidMip{
    Name taskIdentity = s_NameNone;
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

// Copy handles and slots together before declaration; only parameter values vary per frame.
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

    [[nodiscard]] bool hasHardwareWork()const noexcept{ return parameters.hardwareEnabled != 0u && parameters.maxHardwareRays > 0u; }

    [[nodiscard]] bool valid()const noexcept{
        return
            parameters.width > 0u && parameters.height > 0u && opaqueRadiance && glassRadiance
            && queue && counters && indirectArgs && frameParameters && classifyPipeline && buildArgsPipeline
            && depthPyramid.valid()
        ;
    }
};

struct ReflectionCsgDispatchState{
    ReflectionFrameSnapshot resources;
    Core::GpuTimingFrameTransaction timing;

    ReflectionCsgDispatchState(const ReflectionFrameSnapshot& frame, Core::GpuTimingRecorder& recorder)
        : resources(frame)
        , timing(recorder)
    {}
};
using ReflectionCsgDispatchControl = RefCounter<ReflectionCsgDispatchState>;
using ReflectionCsgDispatchHandle = RefCountPtr<
    ReflectionCsgDispatchControl, ArenaRefDeleter<ReflectionCsgDispatchControl, Core::Alloc::GlobalArena>
>;

class RendererReflectionSystem final : NoCopy{
public:
    RendererReflectionSystem(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, RendererShaderSystem& shaders);


public:
    // Caller joins submitted work and discards snapshots before teardown.
    void invalidateResources();
    [[nodiscard]] bool prepareResources(u32 width, u32 height, bool prepareHardware, bool prepareCsgHardware, const ReflectionSettings& settings);
    [[nodiscard]] ReflectionCsgDispatchHandle createCsgDispatchState(const ReflectionFrameSnapshot& resources)const;
    void pollStatistics();
    [[nodiscard]] Expected<ReflectionStatistics> tryGetLatestStatistics()const noexcept;
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
    [[nodiscard]] bool prepareArguments(u32 capacity, bool sliced);
    [[nodiscard]] bool preparePipelines(bool prepareHardware, bool prepareCsgHardware);

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
    static constexpr usize s_ReflectionDescriptorCount = 8u;
    Core::GpuDescriptorHandle m_descriptors[s_ReflectionDescriptorCount];
    Core::GpuDescriptorHandle m_depthSampledDescriptor;
    Core::GpuDescriptorHandle m_depthMipSampledDescriptors[ReflectionDepthPyramidSnapshot::s_MaxMipCount];
    Core::GpuDescriptorHandle m_depthMipStorageDescriptors[ReflectionDepthPyramidSnapshot::s_MaxMipCount];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

