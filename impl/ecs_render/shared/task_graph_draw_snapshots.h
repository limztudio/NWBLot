// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/renderer_draw_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPassCsgGraphSnapshotBase{
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    CsgFrameWorkRegion csgWorkRegion;
    b8 captured = false;


public:
    explicit MaterialPassCsgGraphSnapshotBase(Core::Alloc::GlobalArena& arena)noexcept
        : csgReceiverRanges(arena)
        , csgCutters(arena)
    {}


public:
    void captureCsgFrameData(const CsgFrameGpuDataView& csgFrameData, usize inInstanceCount, usize inMaterialTypedByteCount);

    [[nodiscard]] CsgFrameGpuDataView csgFrameDataView()const noexcept{
        return { csgReceiverRanges, csgCutters, csgWorkRegion };
    }
};


struct TransparentCsgIntervalGraphSnapshot : MaterialPassCsgGraphSnapshotBase{
    DrawItemVector receiverSurfaceMeshDrawItems;
    DrawItemVector receiverSurfaceIndexedDrawItems;
    DrawItemVector receiverSurfaceComputeDrawItems;


public:
    explicit TransparentCsgIntervalGraphSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : MaterialPassCsgGraphSnapshotBase(arena)
        , receiverSurfaceMeshDrawItems(arena)
        , receiverSurfaceIndexedDrawItems(arena)
        , receiverSurfaceComputeDrawItems(arena)
    {}


public:
    void capture(const MaterialPassDrawItemsView& receiverSurfaceDrawItems, const CsgFrameGpuDataView& csgFrameData, usize inInstanceCount, usize inMaterialTypedByteCount);

    [[nodiscard]] MaterialPassDrawItemsView receiverSurfaceDrawItemsView()const noexcept{
        return { receiverSurfaceMeshDrawItems, receiverSurfaceIndexedDrawItems, receiverSurfaceComputeDrawItems };
    }
};


struct TransparentMaterialPassGraphSnapshot : MaterialPassCsgGraphSnapshotBase{
    DrawItemVector regularMeshDrawItems;
    DrawItemVector regularIndexedDrawItems;
    DrawItemVector regularComputeDrawItems;
    DrawItemVector csgMeshDrawItems;
    DrawItemVector csgIndexedDrawItems;
    DrawItemVector csgComputeDrawItems;


public:
    explicit TransparentMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : MaterialPassCsgGraphSnapshotBase(arena)
        , regularMeshDrawItems(arena)
        , regularIndexedDrawItems(arena)
        , regularComputeDrawItems(arena)
        , csgMeshDrawItems(arena)
        , csgIndexedDrawItems(arena)
        , csgComputeDrawItems(arena)
    {}


public:
    void capture(const MaterialPassDrawItemPartitionsView& drawItems, const CsgFrameGpuDataView& csgFrameData, usize inInstanceCount, usize inMaterialTypedByteCount);

    [[nodiscard]] MaterialPassDrawItemsView regularView()const noexcept{
        return { regularMeshDrawItems, regularIndexedDrawItems, regularComputeDrawItems };
    }

    [[nodiscard]] MaterialPassDrawItemsView csgView()const noexcept{
        return { csgMeshDrawItems, csgIndexedDrawItems, csgComputeDrawItems };
    }

    [[nodiscard]] MaterialPassDrawItemPartitionsView drawItemsView()const noexcept{
        return { regularView(), csgView() };
    }
};


struct OpaqueMaterialPassGraphSnapshot : TransparentMaterialPassGraphSnapshot{
    DrawItemVector csgReceiverSurfaceMeshDrawItems;
    DrawItemVector csgReceiverSurfaceIndexedDrawItems;
    DrawItemVector csgReceiverSurfaceComputeDrawItems;


public:
    explicit OpaqueMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : TransparentMaterialPassGraphSnapshot(arena)
        , csgReceiverSurfaceMeshDrawItems(arena)
        , csgReceiverSurfaceIndexedDrawItems(arena)
        , csgReceiverSurfaceComputeDrawItems(arena)
    {}


public:
    void capture(const MaterialPassDrawItemPartitionsView& drawItems, const CsgFrameGpuDataView& csgFrameData, usize inInstanceCount, usize inMaterialTypedByteCount);

    [[nodiscard]] MaterialPassDrawItemsView csgReceiverSurfaceView()const noexcept{
        return { csgReceiverSurfaceMeshDrawItems, csgReceiverSurfaceIndexedDrawItems, csgReceiverSurfaceComputeDrawItems };
    }

    [[nodiscard]] MaterialPassDrawItemPartitionsView drawItemsView()const noexcept{
        return { regularView(), csgView(), csgReceiverSurfaceView() };
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

