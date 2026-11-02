// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_draw_snapshots.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MaterialPassCsgGraphSnapshotBase::captureCsgFrameData(
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount
){
    AssignVector(csgReceiverRanges, csgFrameData.receiverRanges);
    AssignVector(csgCutters, csgFrameData.cutters);
    csgWorkRegion = csgFrameData.workRegion;
    instanceCount = inInstanceCount;
    materialTypedByteCount = inMaterialTypedByteCount;
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
    captureCsgFrameData(csgFrameData, inInstanceCount, inMaterialTypedByteCount);
}

void TransparentMaterialPassGraphSnapshot::capture(
    const MaterialPassDrawItemPartitionsView& drawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount
){
    AssignVector(regularMeshDrawItems, drawItems.regular.meshDrawItems);
    AssignVector(regularIndexedDrawItems, drawItems.regular.indexedDrawItems);
    AssignVector(regularComputeDrawItems, drawItems.regular.computeDrawItems);
    AssignVector(csgMeshDrawItems, drawItems.csg.meshDrawItems);
    AssignVector(csgIndexedDrawItems, drawItems.csg.indexedDrawItems);
    AssignVector(csgComputeDrawItems, drawItems.csg.computeDrawItems);
    captureCsgFrameData(csgFrameData, inInstanceCount, inMaterialTypedByteCount);
}

void OpaqueMaterialPassGraphSnapshot::capture(
    const MaterialPassDrawItemPartitionsView& drawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const usize inInstanceCount,
    const usize inMaterialTypedByteCount
){
    TransparentMaterialPassGraphSnapshot::capture(drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount);
    AssignVector(csgReceiverSurfaceMeshDrawItems, drawItems.csgReceiverSurface.meshDrawItems);
    AssignVector(csgReceiverSurfaceIndexedDrawItems, drawItems.csgReceiverSurface.indexedDrawItems);
    AssignVector(csgReceiverSurfaceComputeDrawItems, drawItems.csgReceiverSurface.computeDrawItems);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

