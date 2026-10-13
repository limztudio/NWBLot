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


// Opaque and transparent snapshots copy the same draw-item, receiver-range, and cutter vectors between frame partitions and graph snapshots.
template<typename DestinationT, typename SourceT>
inline void ReplaceSnapshotVector(DestinationT& destination, const SourceT& source){
    destination.reserve(source.size());
    destination.assign(source.begin(), source.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Snapshot>
inline void CaptureMaterialPassSnapshot(Snapshot& snapshot, const MaterialPassDrawItemPartitions& drawItems, const CsgFrameGpuData& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount){
    ReplaceSnapshotVector(snapshot.regularMeshDrawItems, drawItems.regular.meshDrawItems);
    ReplaceSnapshotVector(snapshot.regularIndexedDrawItems, drawItems.regular.indexedDrawItems);
    ReplaceSnapshotVector(snapshot.regularComputeDrawItems, drawItems.regular.computeDrawItems);
    ReplaceSnapshotVector(snapshot.csgMeshDrawItems, drawItems.csg.meshDrawItems);
    ReplaceSnapshotVector(snapshot.csgIndexedDrawItems, drawItems.csg.indexedDrawItems);
    ReplaceSnapshotVector(snapshot.csgComputeDrawItems, drawItems.csg.computeDrawItems);
    ReplaceSnapshotVector(snapshot.csgReceiverRanges, csgFrameData.receiverRanges);
    ReplaceSnapshotVector(snapshot.csgCutters, csgFrameData.cutters);
    snapshot.csgWorkRegion = csgFrameData.workRegion;
    snapshot.instanceCount = inInstanceCount;
    snapshot.materialTypedByteCount = inMaterialTypedByteCount;
}


template<typename Snapshot>
inline void MaterializeMaterialPassSnapshot(const Snapshot& snapshot, MaterialPassDrawItemPartitions& outDrawItems, CsgFrameGpuData& outCsgFrameData){
    ReplaceSnapshotVector(outDrawItems.regular.meshDrawItems, snapshot.regularMeshDrawItems);
    ReplaceSnapshotVector(outDrawItems.regular.indexedDrawItems, snapshot.regularIndexedDrawItems);
    ReplaceSnapshotVector(outDrawItems.regular.computeDrawItems, snapshot.regularComputeDrawItems);
    ReplaceSnapshotVector(outDrawItems.csg.meshDrawItems, snapshot.csgMeshDrawItems);
    ReplaceSnapshotVector(outDrawItems.csg.indexedDrawItems, snapshot.csgIndexedDrawItems);
    ReplaceSnapshotVector(outDrawItems.csg.computeDrawItems, snapshot.csgComputeDrawItems);
    ReplaceSnapshotVector(outCsgFrameData.receiverRanges, snapshot.csgReceiverRanges);
    ReplaceSnapshotVector(outCsgFrameData.cutters, snapshot.csgCutters);
    outCsgFrameData.workRegion = snapshot.csgWorkRegion;
}



struct OpaqueMaterialPassGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

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

    explicit OpaqueMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)
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

    void capture(const MaterialPassDrawItemPartitions& drawItems, const CsgFrameGpuData& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount){
        CaptureMaterialPassSnapshot(*this, drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount);
        ReplaceSnapshotVector(csgReceiverSurfaceMeshDrawItems, drawItems.csgReceiverSurface.meshDrawItems);
        ReplaceSnapshotVector(csgReceiverSurfaceIndexedDrawItems, drawItems.csgReceiverSurface.indexedDrawItems);
        ReplaceSnapshotVector(csgReceiverSurfaceComputeDrawItems, drawItems.csgReceiverSurface.computeDrawItems);
        captured = true;
    }

    void materialize(MaterialPassDrawItemPartitions& outDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        MaterializeMaterialPassSnapshot(*this, outDrawItems, outCsgFrameData);
        ReplaceSnapshotVector(outDrawItems.csgReceiverSurface.meshDrawItems, csgReceiverSurfaceMeshDrawItems);
        ReplaceSnapshotVector(outDrawItems.csgReceiverSurface.indexedDrawItems, csgReceiverSurfaceIndexedDrawItems);
        ReplaceSnapshotVector(outDrawItems.csgReceiverSurface.computeDrawItems, csgReceiverSurfaceComputeDrawItems);
    }
};


struct TransparentCsgIntervalGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector receiverSurfaceMeshDrawItems;
    DrawItemVector receiverSurfaceIndexedDrawItems;
    DrawItemVector receiverSurfaceComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    CsgFrameWorkRegion csgWorkRegion;
    bool captured = false;

    explicit TransparentCsgIntervalGraphSnapshot(Core::Alloc::GlobalArena& arena)
        : receiverSurfaceMeshDrawItems(arena)
        , receiverSurfaceIndexedDrawItems(arena)
        , receiverSurfaceComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}

    void capture(const MaterialPassDrawItems& receiverSurfaceDrawItems, const CsgFrameGpuData& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount){
        ReplaceSnapshotVector(receiverSurfaceMeshDrawItems, receiverSurfaceDrawItems.meshDrawItems);
        ReplaceSnapshotVector(receiverSurfaceIndexedDrawItems, receiverSurfaceDrawItems.indexedDrawItems);
        ReplaceSnapshotVector(receiverSurfaceComputeDrawItems, receiverSurfaceDrawItems.computeDrawItems);
        ReplaceSnapshotVector(csgReceiverRanges, csgFrameData.receiverRanges);
        ReplaceSnapshotVector(csgCutters, csgFrameData.cutters);
        csgWorkRegion = csgFrameData.workRegion;
        instanceCount = inInstanceCount;
        materialTypedByteCount = inMaterialTypedByteCount;
        captured = true;
    }

    void materialize(MaterialPassDrawItems& outReceiverSurfaceDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        ReplaceSnapshotVector(outReceiverSurfaceDrawItems.meshDrawItems, receiverSurfaceMeshDrawItems);
        ReplaceSnapshotVector(outReceiverSurfaceDrawItems.indexedDrawItems, receiverSurfaceIndexedDrawItems);
        ReplaceSnapshotVector(outReceiverSurfaceDrawItems.computeDrawItems, receiverSurfaceComputeDrawItems);
        materializeCsgFrameData(outCsgFrameData);
    }

    void materializeCsgFrameData(CsgFrameGpuData& outCsgFrameData)const{
        ReplaceSnapshotVector(outCsgFrameData.receiverRanges, csgReceiverRanges);
        ReplaceSnapshotVector(outCsgFrameData.cutters, csgCutters);
        outCsgFrameData.workRegion = csgWorkRegion;
    }
};


struct TransparentMaterialPassGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

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

    explicit TransparentMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)
        : regularMeshDrawItems(arena)
        , regularIndexedDrawItems(arena)
        , regularComputeDrawItems(arena)
        , csgMeshDrawItems(arena)
        , csgIndexedDrawItems(arena)
        , csgComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}

    void capture(const MaterialPassDrawItemPartitions& drawItems, const CsgFrameGpuData& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount){
        CaptureMaterialPassSnapshot(*this, drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount);
        captured = true;
    }

    void materialize(MaterialPassDrawItemPartitions& outDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        MaterializeMaterialPassSnapshot(*this, outDrawItems, outCsgFrameData);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

