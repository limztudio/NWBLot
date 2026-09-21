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

    DrawItemVector regularMeshDrawItems;
    DrawItemVector regularIndexedDrawItems;
    DrawItemVector regularComputeDrawItems;
    DrawItemVector csgMeshDrawItems;
    DrawItemVector csgComputeDrawItems;
    DrawItemVector csgReceiverSurfaceMeshDrawItems;
    DrawItemVector csgReceiverSurfaceComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    CsgFrameWorkRegion csgWorkRegion;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool captured = false;

    explicit OpaqueMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)
        : regularMeshDrawItems(arena)
        , regularIndexedDrawItems(arena)
        , regularComputeDrawItems(arena)
        , csgMeshDrawItems(arena)
        , csgComputeDrawItems(arena)
        , csgReceiverSurfaceMeshDrawItems(arena)
        , csgReceiverSurfaceComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}

    void capture(
        const MaterialPassDrawItemPartitions& drawItems,
        const CsgFrameGpuData& csgFrameData,
        const usize inInstanceCount,
        const usize inMaterialTypedByteCount
    ){
        regularMeshDrawItems.reserve(drawItems.regular.meshDrawItems.size());
        regularMeshDrawItems.assign(drawItems.regular.meshDrawItems.begin(), drawItems.regular.meshDrawItems.end());
        regularIndexedDrawItems.reserve(drawItems.regular.indexedDrawItems.size());
        regularIndexedDrawItems.assign(drawItems.regular.indexedDrawItems.begin(), drawItems.regular.indexedDrawItems.end());
        regularComputeDrawItems.reserve(drawItems.regular.computeDrawItems.size());
        regularComputeDrawItems.assign(drawItems.regular.computeDrawItems.begin(), drawItems.regular.computeDrawItems.end());
        csgMeshDrawItems.reserve(drawItems.csg.meshDrawItems.size());
        csgMeshDrawItems.assign(drawItems.csg.meshDrawItems.begin(), drawItems.csg.meshDrawItems.end());
        csgComputeDrawItems.reserve(drawItems.csg.computeDrawItems.size());
        csgComputeDrawItems.assign(drawItems.csg.computeDrawItems.begin(), drawItems.csg.computeDrawItems.end());
        csgReceiverSurfaceMeshDrawItems.reserve(drawItems.csgReceiverSurface.meshDrawItems.size());
        csgReceiverSurfaceMeshDrawItems.assign(
            drawItems.csgReceiverSurface.meshDrawItems.begin(),
            drawItems.csgReceiverSurface.meshDrawItems.end()
        );
        csgReceiverSurfaceComputeDrawItems.reserve(drawItems.csgReceiverSurface.computeDrawItems.size());
        csgReceiverSurfaceComputeDrawItems.assign(
            drawItems.csgReceiverSurface.computeDrawItems.begin(),
            drawItems.csgReceiverSurface.computeDrawItems.end()
        );
        csgReceiverRanges.reserve(csgFrameData.receiverRanges.size());
        csgReceiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        csgCutters.reserve(csgFrameData.cutters.size());
        csgCutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        csgWorkRegion = csgFrameData.workRegion;
        instanceCount = inInstanceCount;
        materialTypedByteCount = inMaterialTypedByteCount;
        captured = true;
    }

    void materialize(
        MaterialPassDrawItemPartitions& outDrawItems,
        CsgFrameGpuData& outCsgFrameData
    )const{
        outDrawItems.regular.meshDrawItems.reserve(regularMeshDrawItems.size());
        outDrawItems.regular.meshDrawItems.assign(regularMeshDrawItems.begin(), regularMeshDrawItems.end());
        outDrawItems.regular.indexedDrawItems.reserve(regularIndexedDrawItems.size());
        outDrawItems.regular.indexedDrawItems.assign(regularIndexedDrawItems.begin(), regularIndexedDrawItems.end());
        outDrawItems.regular.computeDrawItems.reserve(regularComputeDrawItems.size());
        outDrawItems.regular.computeDrawItems.assign(regularComputeDrawItems.begin(), regularComputeDrawItems.end());
        outDrawItems.csg.meshDrawItems.reserve(csgMeshDrawItems.size());
        outDrawItems.csg.meshDrawItems.assign(csgMeshDrawItems.begin(), csgMeshDrawItems.end());
        outDrawItems.csg.computeDrawItems.reserve(csgComputeDrawItems.size());
        outDrawItems.csg.computeDrawItems.assign(csgComputeDrawItems.begin(), csgComputeDrawItems.end());
        outDrawItems.csgReceiverSurface.meshDrawItems.reserve(csgReceiverSurfaceMeshDrawItems.size());
        outDrawItems.csgReceiverSurface.meshDrawItems.assign(
            csgReceiverSurfaceMeshDrawItems.begin(),
            csgReceiverSurfaceMeshDrawItems.end()
        );
        outDrawItems.csgReceiverSurface.computeDrawItems.reserve(csgReceiverSurfaceComputeDrawItems.size());
        outDrawItems.csgReceiverSurface.computeDrawItems.assign(
            csgReceiverSurfaceComputeDrawItems.begin(),
            csgReceiverSurfaceComputeDrawItems.end()
        );
        outCsgFrameData.receiverRanges.reserve(csgReceiverRanges.size());
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
        outCsgFrameData.cutters.reserve(csgCutters.size());
        outCsgFrameData.cutters.assign(csgCutters.begin(), csgCutters.end());
        outCsgFrameData.workRegion = csgWorkRegion;
    }
};


struct TransparentCsgIntervalGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector receiverSurfaceMeshDrawItems;
    DrawItemVector receiverSurfaceComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    CsgFrameWorkRegion csgWorkRegion;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool captured = false;

    explicit TransparentCsgIntervalGraphSnapshot(Core::Alloc::GlobalArena& arena)
        : receiverSurfaceMeshDrawItems(arena)
        , receiverSurfaceComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}

    void capture(
        const MaterialPassDrawItems& receiverSurfaceDrawItems,
        const CsgFrameGpuData& csgFrameData,
        const usize inInstanceCount,
        const usize inMaterialTypedByteCount
    ){
        receiverSurfaceMeshDrawItems.reserve(receiverSurfaceDrawItems.meshDrawItems.size());
        receiverSurfaceMeshDrawItems.assign(
            receiverSurfaceDrawItems.meshDrawItems.begin(),
            receiverSurfaceDrawItems.meshDrawItems.end()
        );
        receiverSurfaceComputeDrawItems.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
        receiverSurfaceComputeDrawItems.assign(
            receiverSurfaceDrawItems.computeDrawItems.begin(),
            receiverSurfaceDrawItems.computeDrawItems.end()
        );
        csgReceiverRanges.reserve(csgFrameData.receiverRanges.size());
        csgReceiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        csgCutters.reserve(csgFrameData.cutters.size());
        csgCutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        csgWorkRegion = csgFrameData.workRegion;
        instanceCount = inInstanceCount;
        materialTypedByteCount = inMaterialTypedByteCount;
        captured = true;
    }

    void materialize(
        MaterialPassDrawItems& outReceiverSurfaceDrawItems,
        CsgFrameGpuData& outCsgFrameData
    )const{
        outReceiverSurfaceDrawItems.meshDrawItems.reserve(receiverSurfaceMeshDrawItems.size());
        outReceiverSurfaceDrawItems.meshDrawItems.assign(
            receiverSurfaceMeshDrawItems.begin(),
            receiverSurfaceMeshDrawItems.end()
        );
        outReceiverSurfaceDrawItems.computeDrawItems.reserve(receiverSurfaceComputeDrawItems.size());
        outReceiverSurfaceDrawItems.computeDrawItems.assign(
            receiverSurfaceComputeDrawItems.begin(),
            receiverSurfaceComputeDrawItems.end()
        );
        materializeCsgFrameData(outCsgFrameData);
    }

    void materializeCsgFrameData(CsgFrameGpuData& outCsgFrameData)const{
        outCsgFrameData.receiverRanges.reserve(csgReceiverRanges.size());
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
        outCsgFrameData.cutters.reserve(csgCutters.size());
        outCsgFrameData.cutters.assign(csgCutters.begin(), csgCutters.end());
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
    DrawItemVector csgComputeDrawItems;
    ReceiverRangeVector csgReceiverRanges;
    CutterVector csgCutters;
    CsgFrameWorkRegion csgWorkRegion;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool captured = false;

    explicit TransparentMaterialPassGraphSnapshot(Core::Alloc::GlobalArena& arena)
        : regularMeshDrawItems(arena)
        , regularIndexedDrawItems(arena)
        , regularComputeDrawItems(arena)
        , csgMeshDrawItems(arena)
        , csgComputeDrawItems(arena)
        , csgReceiverRanges(arena)
        , csgCutters(arena)
    {}

    void capture(
        const MaterialPassDrawItemPartitions& drawItems,
        const CsgFrameGpuData& csgFrameData,
        const usize inInstanceCount,
        const usize inMaterialTypedByteCount
    ){
        regularMeshDrawItems.reserve(drawItems.regular.meshDrawItems.size());
        regularMeshDrawItems.assign(drawItems.regular.meshDrawItems.begin(), drawItems.regular.meshDrawItems.end());
        regularIndexedDrawItems.reserve(drawItems.regular.indexedDrawItems.size());
        regularIndexedDrawItems.assign(drawItems.regular.indexedDrawItems.begin(), drawItems.regular.indexedDrawItems.end());
        regularComputeDrawItems.reserve(drawItems.regular.computeDrawItems.size());
        regularComputeDrawItems.assign(drawItems.regular.computeDrawItems.begin(), drawItems.regular.computeDrawItems.end());
        csgMeshDrawItems.reserve(drawItems.csg.meshDrawItems.size());
        csgMeshDrawItems.assign(drawItems.csg.meshDrawItems.begin(), drawItems.csg.meshDrawItems.end());
        csgComputeDrawItems.reserve(drawItems.csg.computeDrawItems.size());
        csgComputeDrawItems.assign(drawItems.csg.computeDrawItems.begin(), drawItems.csg.computeDrawItems.end());
        csgReceiverRanges.reserve(csgFrameData.receiverRanges.size());
        csgReceiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
        csgCutters.reserve(csgFrameData.cutters.size());
        csgCutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
        csgWorkRegion = csgFrameData.workRegion;
        instanceCount = inInstanceCount;
        materialTypedByteCount = inMaterialTypedByteCount;
        captured = true;
    }

    void materialize(
        MaterialPassDrawItemPartitions& outDrawItems,
        CsgFrameGpuData& outCsgFrameData
    )const{
        outDrawItems.regular.meshDrawItems.reserve(regularMeshDrawItems.size());
        outDrawItems.regular.meshDrawItems.assign(regularMeshDrawItems.begin(), regularMeshDrawItems.end());
        outDrawItems.regular.indexedDrawItems.reserve(regularIndexedDrawItems.size());
        outDrawItems.regular.indexedDrawItems.assign(regularIndexedDrawItems.begin(), regularIndexedDrawItems.end());
        outDrawItems.regular.computeDrawItems.reserve(regularComputeDrawItems.size());
        outDrawItems.regular.computeDrawItems.assign(regularComputeDrawItems.begin(), regularComputeDrawItems.end());
        outDrawItems.csg.meshDrawItems.reserve(csgMeshDrawItems.size());
        outDrawItems.csg.meshDrawItems.assign(csgMeshDrawItems.begin(), csgMeshDrawItems.end());
        outDrawItems.csg.computeDrawItems.reserve(csgComputeDrawItems.size());
        outDrawItems.csg.computeDrawItems.assign(csgComputeDrawItems.begin(), csgComputeDrawItems.end());
        outCsgFrameData.receiverRanges.reserve(csgReceiverRanges.size());
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
        outCsgFrameData.cutters.reserve(csgCutters.size());
        outCsgFrameData.cutters.assign(csgCutters.begin(), csgCutters.end());
        outCsgFrameData.workRegion = csgWorkRegion;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

