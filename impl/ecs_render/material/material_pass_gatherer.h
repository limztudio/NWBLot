// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "material_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialPassGatherer final : NoCopy{
public:
    MaterialPassGatherer(RendererMaterialSystem& system, Core::ECS::World& world, RendererMeshSystem& meshSystem, RendererCsgSystem& csgSystem, CsgShapeRegistry& csgShapeRegistry, const RendererOpticalVolumeSelection& opticalVolumes)noexcept
        : m_system(system), m_world(world), m_meshSystem(meshSystem), m_csgSystem(csgSystem), m_csgShapeRegistry(csgShapeRegistry), m_opticalVolumes(opticalVolumes)
    {}


public:
    void gather(
        Core::Framebuffer* framebuffer,
        const MaterialPipelinePass::Enum pass,
        const b8 transparent,
        const CsgFrameState& csgFrameState,
        const RendererResourceLookupMode::Enum lookupMode,
        const MeshViewGpuData* const csgWorkRegionMeshViewState,
        MaterialPassDrawItemPartitions& drawItems,
        InstanceGpuDataVector& instanceData,
        CsgFrameGpuData& csgFrameData,
    #if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
    #endif
        MaterialTypedByteDataVector& materialTypedBytes
    );


private:
    RendererMaterialSystem& m_system;
    Core::ECS::World& m_world;
    RendererMeshSystem& m_meshSystem;
    RendererCsgSystem& m_csgSystem;
    CsgShapeRegistry& m_csgShapeRegistry;
    const RendererOpticalVolumeSelection& m_opticalVolumes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

