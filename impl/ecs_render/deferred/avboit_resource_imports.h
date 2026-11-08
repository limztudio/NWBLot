// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/deferred/deferred_frame_targets.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredGraphResourceImportResult;


class DeferredAvboitResourceImports final{
public:
    void capture(Core::GpuTaskGraph& graph, const DeferredFrameTargets& deferredTargets, b8 clearTargets);
    [[nodiscard]] b8 validRefractionInputs()const noexcept;
    [[nodiscard]] b8 validRefractionSpecular()const noexcept;
    [[nodiscard]] b8 validAvboitInputs()const noexcept;
    void publish(DeferredGraphResourceImportResult& result)const noexcept;


private:
    Expected<Core::GpuGraphResourceId> m_avboitLowRaster = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitAccumColor = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitAccumExtinction = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_refractionDepth = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_refractionNormalIor = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_refractionTintCoverage = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_refractionInstance = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_refractionSpecularRoughness = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_refractionResolve = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitForegroundColor = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitForegroundExtinction = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitTransmittance = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitCoverage = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitDepthWarp = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitControl = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitExtinction = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitExtinctionOverflow = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitMaterialDomain = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> m_avboitCsgDomain = MakeUnexpected(Failure{});
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

