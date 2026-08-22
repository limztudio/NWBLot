// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/renderer_private.h>


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
        regularMeshDrawItems.assign(drawItems.regular.meshDrawItems.begin(), drawItems.regular.meshDrawItems.end());
        regularComputeDrawItems.assign(drawItems.regular.computeDrawItems.begin(), drawItems.regular.computeDrawItems.end());
        csgMeshDrawItems.assign(drawItems.csg.meshDrawItems.begin(), drawItems.csg.meshDrawItems.end());
        csgComputeDrawItems.assign(drawItems.csg.computeDrawItems.begin(), drawItems.csg.computeDrawItems.end());
        csgReceiverSurfaceMeshDrawItems.assign(
            drawItems.csgReceiverSurface.meshDrawItems.begin(),
            drawItems.csgReceiverSurface.meshDrawItems.end()
        );
        csgReceiverSurfaceComputeDrawItems.assign(
            drawItems.csgReceiverSurface.computeDrawItems.begin(),
            drawItems.csgReceiverSurface.computeDrawItems.end()
        );
        csgReceiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
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
        outDrawItems.regular.meshDrawItems.assign(regularMeshDrawItems.begin(), regularMeshDrawItems.end());
        outDrawItems.regular.computeDrawItems.assign(regularComputeDrawItems.begin(), regularComputeDrawItems.end());
        outDrawItems.csg.meshDrawItems.assign(csgMeshDrawItems.begin(), csgMeshDrawItems.end());
        outDrawItems.csg.computeDrawItems.assign(csgComputeDrawItems.begin(), csgComputeDrawItems.end());
        outDrawItems.csgReceiverSurface.meshDrawItems.assign(
            csgReceiverSurfaceMeshDrawItems.begin(),
            csgReceiverSurfaceMeshDrawItems.end()
        );
        outDrawItems.csgReceiverSurface.computeDrawItems.assign(
            csgReceiverSurfaceComputeDrawItems.begin(),
            csgReceiverSurfaceComputeDrawItems.end()
        );
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
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
        receiverSurfaceMeshDrawItems.assign(
            receiverSurfaceDrawItems.meshDrawItems.begin(),
            receiverSurfaceDrawItems.meshDrawItems.end()
        );
        receiverSurfaceComputeDrawItems.assign(
            receiverSurfaceDrawItems.computeDrawItems.begin(),
            receiverSurfaceDrawItems.computeDrawItems.end()
        );
        csgReceiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
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
        outReceiverSurfaceDrawItems.meshDrawItems.assign(
            receiverSurfaceMeshDrawItems.begin(),
            receiverSurfaceMeshDrawItems.end()
        );
        outReceiverSurfaceDrawItems.computeDrawItems.assign(
            receiverSurfaceComputeDrawItems.begin(),
            receiverSurfaceComputeDrawItems.end()
        );
        materializeCsgFrameData(outCsgFrameData);
    }

    void materializeCsgFrameData(CsgFrameGpuData& outCsgFrameData)const{
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
        outCsgFrameData.cutters.assign(csgCutters.begin(), csgCutters.end());
        outCsgFrameData.workRegion = csgWorkRegion;
    }
};


struct TransparentMaterialPassGraphSnapshot{
    using DrawItemVector = Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena>;
    using ReceiverRangeVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::GlobalArena>;
    using CutterVector = Vector<CsgCutterGpuData, Core::Alloc::GlobalArena>;

    DrawItemVector regularMeshDrawItems;
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
        regularMeshDrawItems.assign(drawItems.regular.meshDrawItems.begin(), drawItems.regular.meshDrawItems.end());
        regularComputeDrawItems.assign(drawItems.regular.computeDrawItems.begin(), drawItems.regular.computeDrawItems.end());
        csgMeshDrawItems.assign(drawItems.csg.meshDrawItems.begin(), drawItems.csg.meshDrawItems.end());
        csgComputeDrawItems.assign(drawItems.csg.computeDrawItems.begin(), drawItems.csg.computeDrawItems.end());
        csgReceiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
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
        outDrawItems.regular.meshDrawItems.assign(regularMeshDrawItems.begin(), regularMeshDrawItems.end());
        outDrawItems.regular.computeDrawItems.assign(regularComputeDrawItems.begin(), regularComputeDrawItems.end());
        outDrawItems.csg.meshDrawItems.assign(csgMeshDrawItems.begin(), csgMeshDrawItems.end());
        outDrawItems.csg.computeDrawItems.assign(csgComputeDrawItems.begin(), csgComputeDrawItems.end());
        outCsgFrameData.receiverRanges.assign(csgReceiverRanges.begin(), csgReceiverRanges.end());
        outCsgFrameData.cutters.assign(csgCutters.begin(), csgCutters.end());
        outCsgFrameData.workRegion = csgWorkRegion;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

