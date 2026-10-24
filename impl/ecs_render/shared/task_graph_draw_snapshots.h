// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/renderer_draw_types.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Snapshot>
inline void CaptureMaterialPassSnapshot(const MaterialPassDrawItemPartitionsView& drawItems, const CsgFrameGpuDataView& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount, Snapshot& snapshot)noexcept(
    noexcept(AssignVector(snapshot.regularMeshDrawItems, drawItems.regular.meshDrawItems))
    && noexcept(AssignVector(snapshot.regularIndexedDrawItems, drawItems.regular.indexedDrawItems))
    && noexcept(AssignVector(snapshot.regularComputeDrawItems, drawItems.regular.computeDrawItems))
    && noexcept(AssignVector(snapshot.csgMeshDrawItems, drawItems.csg.meshDrawItems))
    && noexcept(AssignVector(snapshot.csgIndexedDrawItems, drawItems.csg.indexedDrawItems))
    && noexcept(AssignVector(snapshot.csgComputeDrawItems, drawItems.csg.computeDrawItems))
    && noexcept(AssignVector(snapshot.csgReceiverRanges, csgFrameData.receiverRanges))
    && noexcept(AssignVector(snapshot.csgCutters, csgFrameData.cutters))
    && noexcept(snapshot.csgWorkRegion = csgFrameData.workRegion)
    && noexcept(snapshot.instanceCount = inInstanceCount)
    && noexcept(snapshot.materialTypedByteCount = inMaterialTypedByteCount)
){
    AssignVector(snapshot.regularMeshDrawItems, drawItems.regular.meshDrawItems);
    AssignVector(snapshot.regularIndexedDrawItems, drawItems.regular.indexedDrawItems);
    AssignVector(snapshot.regularComputeDrawItems, drawItems.regular.computeDrawItems);
    AssignVector(snapshot.csgMeshDrawItems, drawItems.csg.meshDrawItems);
    AssignVector(snapshot.csgIndexedDrawItems, drawItems.csg.indexedDrawItems);
    AssignVector(snapshot.csgComputeDrawItems, drawItems.csg.computeDrawItems);
    AssignVector(snapshot.csgReceiverRanges, csgFrameData.receiverRanges);
    AssignVector(snapshot.csgCutters, csgFrameData.cutters);
    snapshot.csgWorkRegion = csgFrameData.workRegion;
    snapshot.instanceCount = inInstanceCount;
    snapshot.materialTypedByteCount = inMaterialTypedByteCount;
}


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
    bool captured = false;


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
    void capture(const MaterialPassDrawItemPartitionsView& drawItems, const CsgFrameGpuDataView& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount){
        CaptureMaterialPassSnapshot(drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount, *this);
        AssignVector(csgReceiverSurfaceMeshDrawItems, drawItems.csgReceiverSurface.meshDrawItems);
        AssignVector(csgReceiverSurfaceIndexedDrawItems, drawItems.csgReceiverSurface.indexedDrawItems);
        AssignVector(csgReceiverSurfaceComputeDrawItems, drawItems.csgReceiverSurface.computeDrawItems);
        captured = true;
    }

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
    bool captured = false;


public:
    explicit TransparentCsgIntervalGraphSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : receiverSurfaceMeshDrawItems(arena)
        , receiverSurfaceIndexedDrawItems(arena)
        , receiverSurfaceComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}


public:
    void capture(const MaterialPassDrawItemsView& receiverSurfaceDrawItems, const CsgFrameGpuDataView& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount){
        AssignVector(receiverSurfaceMeshDrawItems, receiverSurfaceDrawItems.meshDrawItems);
        AssignVector(receiverSurfaceIndexedDrawItems, receiverSurfaceDrawItems.indexedDrawItems);
        AssignVector(receiverSurfaceComputeDrawItems, receiverSurfaceDrawItems.computeDrawItems);
        AssignVector(csgReceiverRanges, csgFrameData.receiverRanges);
        AssignVector(csgCutters, csgFrameData.cutters);
        csgWorkRegion = csgFrameData.workRegion;
        instanceCount = inInstanceCount;
        materialTypedByteCount = inMaterialTypedByteCount;
        captured = true;
    }

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
    bool captured = false;


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
    void capture(const MaterialPassDrawItemPartitionsView& drawItems, const CsgFrameGpuDataView& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount){
        CaptureMaterialPassSnapshot(drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount, *this);
        captured = true;
    }

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

