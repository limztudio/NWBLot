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


struct OpaqueMaterialPassGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;


public:
    DrawItemVector regularMeshDrawItems;
    DrawItemVector regularIndexedDrawItems;
    DrawItemVector regularComputeDrawItems;
    DrawItemVector csgMeshDrawItems;
    DrawItemVector csgIndexedDrawItems;
    DrawItemVector csgComputeDrawItems;
    DrawItemVector csgReceiverSurfaceMeshDrawItems;
    DrawItemVector csgReceiverSurfaceIndexedDrawItems;
    DrawItemVector csgReceiverSurfaceComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    CsgFrameWorkRegion csgWorkRegion;
    b8 captured = false;


public:
    explicit OpaqueMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : regularMeshDrawItems(arena)
        , regularIndexedDrawItems(arena)
        , regularComputeDrawItems(arena)
        , csgMeshDrawItems(arena)
        , csgIndexedDrawItems(arena)
        , csgComputeDrawItems(arena)
        , csgReceiverSurfaceMeshDrawItems(arena)
        , csgReceiverSurfaceIndexedDrawItems(arena)
        , csgReceiverSurfaceComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}


public:
    void capture(const MaterialPassDrawItemPartitionsView& drawItems, const CsgFrameGpuDataView& csgFrameData, usize inInstanceCount, usize inMaterialTypedByteCount);

    [[nodiscard]] MaterialPassDrawItemsView regularView()const noexcept{
        return { regularMeshDrawItems, regularIndexedDrawItems, regularComputeDrawItems };
    }

    [[nodiscard]] MaterialPassDrawItemsView csgView()const noexcept{
        return { csgMeshDrawItems, csgIndexedDrawItems, csgComputeDrawItems };
    }

    [[nodiscard]] MaterialPassDrawItemsView csgReceiverSurfaceView()const noexcept{
        return { csgReceiverSurfaceMeshDrawItems, csgReceiverSurfaceIndexedDrawItems, csgReceiverSurfaceComputeDrawItems };
    }

    [[nodiscard]] MaterialPassDrawItemPartitionsView drawItemsView()const noexcept{
        return { regularView(), csgView(), csgReceiverSurfaceView() };
    }

    [[nodiscard]] CsgFrameGpuDataView csgFrameDataView()const noexcept{
        return { csgReceiverRanges, csgCutters, csgWorkRegion };
    }
};


struct TransparentCsgIntervalGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;


public:
    DrawItemVector receiverSurfaceMeshDrawItems;
    DrawItemVector receiverSurfaceIndexedDrawItems;
    DrawItemVector receiverSurfaceComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    CsgFrameWorkRegion csgWorkRegion;
    b8 captured = false;


public:
    explicit TransparentCsgIntervalGraphSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : receiverSurfaceMeshDrawItems(arena)
        , receiverSurfaceIndexedDrawItems(arena)
        , receiverSurfaceComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}


public:
    void capture(const MaterialPassDrawItemsView& receiverSurfaceDrawItems, const CsgFrameGpuDataView& csgFrameData, usize inInstanceCount, usize inMaterialTypedByteCount);

    [[nodiscard]] MaterialPassDrawItemsView receiverSurfaceDrawItemsView()const noexcept{
        return { receiverSurfaceMeshDrawItems, receiverSurfaceIndexedDrawItems, receiverSurfaceComputeDrawItems };
    }

    [[nodiscard]] CsgFrameGpuDataView csgFrameDataView()const noexcept{
        return { csgReceiverRanges, csgCutters, csgWorkRegion };
    }
};


struct TransparentMaterialPassGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;


public:
    DrawItemVector regularMeshDrawItems;
    DrawItemVector regularIndexedDrawItems;
    DrawItemVector regularComputeDrawItems;
    DrawItemVector csgMeshDrawItems;
    DrawItemVector csgIndexedDrawItems;
    DrawItemVector csgComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    CsgFrameWorkRegion csgWorkRegion;
    b8 captured = false;


public:
    explicit TransparentMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : regularMeshDrawItems(arena)
        , regularIndexedDrawItems(arena)
        , regularComputeDrawItems(arena)
        , csgMeshDrawItems(arena)
        , csgIndexedDrawItems(arena)
        , csgComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
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

    [[nodiscard]] CsgFrameGpuDataView csgFrameDataView()const noexcept{
        return { csgReceiverRanges, csgCutters, csgWorkRegion };
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

