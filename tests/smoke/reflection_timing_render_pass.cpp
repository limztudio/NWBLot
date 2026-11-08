// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_timing_render_pass.h"

#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/reflection/task_graph_reflection.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionTimingRenderPass::ReflectionTimingRenderPass(Core::GraphicsRuntime& graphics)noexcept
    : IRenderPass(graphics)
{}

b8 ReflectionTimingRenderPass::prepareQueries(const u32 width, const u32 height){
    u32 mipCount = 0u;
    for(u32 extent = Max(width, height); extent > 0u; extent >>= 1u)
        ++mipCount;
    if(width == 0u || height == 0u || mipCount > NWB_REFLECTION_MAX_DEPTH_MIPS){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: invalid extent for depth timing capacity"));
        return false;
    }
    const Array<Name, 17> singleRangeScopes = {
        NWB::Impl::RendererGpuTimingScope::s_Frame.identity,
        NWB::Impl::RendererGpuTimingScope::s_OpaqueRegular.identity,
        NWB::Impl::RendererGpuTimingScope::s_ShadowVisibility.identity,
        NWB::Impl::RendererGpuTimingScope::s_DeferredLighting.identity,
        NWB::Impl::RendererGpuTimingScope::s_DeferredComposite.identity,
        NWB::Impl::RendererGpuTimingScope::s_DeferredPresent.identity,
        NWB::Impl::RendererGpuTimingScope::s_AvboitClear.identity,
        NWB::Impl::RendererGpuTimingScope::s_AvboitOccupancy.identity,
        NWB::Impl::RendererGpuTimingScope::s_AvboitDepthWarp.identity,
        NWB::Impl::RendererGpuTimingScope::s_AvboitExtinction.identity,
        NWB::Impl::RendererGpuTimingScope::s_AvboitIntegration.identity,
        NWB::Impl::RendererGpuTimingScope::s_AvboitAccumulate.identity,
        NWB::Impl::ReflectionGpuTimingScope::s_Classify.identity,
        NWB::Impl::ReflectionGpuTimingScope::s_BuildArgs.identity,
        NWB::Impl::ReflectionGpuTimingScope::s_Hardware.identity,
        NWB::Impl::ReflectionGpuTimingScope::s_Temporal.identity,
        NWB::Impl::ReflectionGpuTimingScope::s_Spatial.identity,
    };
    auto& graphics = getGraphics();
    auto& timing = graphics.gpuTiming();
    auto& device = graphics.getDevice();
    // Reserve enough timer queries to cover all in-flight passes and depth mips.
    for(const Name& scope : singleRangeScopes){
        if(!timing.prepareScopeQueries(scope, device, s_InFlightRanges)){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: failed to prepare timing scope '{}'"), ConvertString(scope.resolvedText()));
            return false;
        }
    }
    if(!timing.prepareScopeQueries(NWB::Impl::ReflectionGpuTimingScope::s_DepthPyramid.identity, device, s_InFlightRanges * mipCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: failed to prepare depth timing capacity"));
        return false;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: timing in-flight ranges {}"), s_InFlightRanges);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: timing depth mip count {}"), mipCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

