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
inline void CaptureMaterialPassSnapshot(Snapshot& snapshot, const MaterialPassDrawItemPartitions& drawItems, const CsgFrameGpuData& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount)noexcept(
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


template<typename Snapshot>
inline void MaterializeMaterialPassSnapshot(const Snapshot& snapshot, MaterialPassDrawItemPartitions& outDrawItems, CsgFrameGpuData& outCsgFrameData){
    AssignVector(outDrawItems.regular.meshDrawItems, snapshot.regularMeshDrawItems);
    AssignVector(outDrawItems.regular.indexedDrawItems, snapshot.regularIndexedDrawItems);
    AssignVector(outDrawItems.regular.computeDrawItems, snapshot.regularComputeDrawItems);
    AssignVector(outDrawItems.csg.meshDrawItems, snapshot.csgMeshDrawItems);
    AssignVector(outDrawItems.csg.indexedDrawItems, snapshot.csgIndexedDrawItems);
    AssignVector(outDrawItems.csg.computeDrawItems, snapshot.csgComputeDrawItems);
    AssignVector(outCsgFrameData.receiverRanges, snapshot.csgReceiverRanges);
    AssignVector(outCsgFrameData.cutters, snapshot.csgCutters);
    outCsgFrameData.workRegion = snapshot.csgWorkRegion;
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
    void capture(const MaterialPassDrawItemPartitions& drawItems, const CsgFrameGpuData& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount){
        CaptureMaterialPassSnapshot(*this, drawItems, csgFrameData, inInstanceCount, inMaterialTypedByteCount);
        AssignVector(csgReceiverSurfaceMeshDrawItems, drawItems.csgReceiverSurface.meshDrawItems);
        AssignVector(csgReceiverSurfaceIndexedDrawItems, drawItems.csgReceiverSurface.indexedDrawItems);
        AssignVector(csgReceiverSurfaceComputeDrawItems, drawItems.csgReceiverSurface.computeDrawItems);
        captured = true;
    }

    void materialize(MaterialPassDrawItemPartitions& outDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        MaterializeMaterialPassSnapshot(*this, outDrawItems, outCsgFrameData);
        AssignVector(outDrawItems.csgReceiverSurface.meshDrawItems, csgReceiverSurfaceMeshDrawItems);
        AssignVector(outDrawItems.csgReceiverSurface.indexedDrawItems, csgReceiverSurfaceIndexedDrawItems);
        AssignVector(outDrawItems.csgReceiverSurface.computeDrawItems, csgReceiverSurfaceComputeDrawItems);
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
    void capture(const MaterialPassDrawItems& receiverSurfaceDrawItems, const CsgFrameGpuData& csgFrameData, const usize inInstanceCount, const usize inMaterialTypedByteCount){
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

    void materialize(MaterialPassDrawItems& outReceiverSurfaceDrawItems, CsgFrameGpuData& outCsgFrameData)const{
        AssignVector(outReceiverSurfaceDrawItems.meshDrawItems, receiverSurfaceMeshDrawItems);
        AssignVector(outReceiverSurfaceDrawItems.indexedDrawItems, receiverSurfaceIndexedDrawItems);
        AssignVector(outReceiverSurfaceDrawItems.computeDrawItems, receiverSurfaceComputeDrawItems);
        materializeCsgFrameData(outCsgFrameData);
    }

    void materializeCsgFrameData(CsgFrameGpuData& outCsgFrameData)const{
        AssignVector(outCsgFrameData.receiverRanges, csgReceiverRanges);
        AssignVector(outCsgFrameData.cutters, csgCutters);
        outCsgFrameData.workRegion = csgWorkRegion;
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

