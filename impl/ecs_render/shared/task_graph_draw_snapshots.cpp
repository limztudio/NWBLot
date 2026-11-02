// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_draw_snapshots.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_draw_snapshots{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Snapshot>
void CaptureMaterialPassSnapshot(
    const MaterialPassDrawItemPartitionsView& drawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount,
    Snapshot& snapshot
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueMaterialPassGraphSnapshot::capture(
    const MaterialPassDrawItemPartitionsView& drawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount
){
    __hidden_task_graph_draw_snapshots::CaptureMaterialPassSnapshot(drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount, *this);
    AssignVector(csgReceiverSurfaceMeshDrawItems, drawItems.csgReceiverSurface.meshDrawItems);
    AssignVector(csgReceiverSurfaceIndexedDrawItems, drawItems.csgReceiverSurface.indexedDrawItems);
    AssignVector(csgReceiverSurfaceComputeDrawItems, drawItems.csgReceiverSurface.computeDrawItems);
    captured = true;
}

void TransparentCsgIntervalGraphSnapshot::capture(
    const MaterialPassDrawItemsView& receiverSurfaceDrawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount
){
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

void TransparentMaterialPassGraphSnapshot::capture(
    const MaterialPassDrawItemPartitionsView& drawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount
){
    __hidden_task_graph_draw_snapshots::CaptureMaterialPassSnapshot(drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount, *this);
    captured = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

