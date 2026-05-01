// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_debug_draw.h"
#include "deformable_operator_shape.h"
#include "deformable_picking.h"
#include "deformable_surface_edit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSDeformableEdit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using DeformablePickingInputs = NWB::Impl::DeformablePickingInputs;
using DeformablePickingRay = NWB::Impl::DeformablePickingRay;
using DeformablePosedHit = NWB::Impl::DeformablePosedHit;
inline constexpr u32 s_DeformableOperatorFootprintMaxVertexCount =
    NWB::Impl::s_DeformableOperatorFootprintMaxVertexCount;
inline constexpr u32 s_DeformableOperatorProfileMaxSampleCount =
    NWB::Impl::s_DeformableOperatorProfileMaxSampleCount;
using DeformableOperatorFootprint = NWB::Impl::DeformableOperatorFootprint;
using DeformableOperatorProfileSample = NWB::Impl::DeformableOperatorProfileSample;
using DeformableOperatorProfile = NWB::Impl::DeformableOperatorProfile;
using DeformableHoleEditParams = NWB::Impl::DeformableHoleEditParams;
using DeformableHoleEditResult = NWB::Impl::DeformableHoleEditResult;
using DeformableSurfaceEditSession = NWB::Impl::DeformableSurfaceEditSession;
using DeformableHolePreview = NWB::Impl::DeformableHolePreview;
using DeformableHolePreviewMeshVertex = NWB::Impl::DeformableHolePreviewMeshVertex;
using DeformableHolePreviewMesh = NWB::Impl::DeformableHolePreviewMesh;
using DeformableSurfaceEditId = NWB::Impl::DeformableSurfaceEditId;
using DeformableSurfaceEditRecord = NWB::Impl::DeformableSurfaceEditRecord;
using DeformableSurfaceEditState = NWB::Impl::DeformableSurfaceEditState;
using DeformableSurfaceEditReplayContext = NWB::Impl::DeformableSurfaceEditReplayContext;
using DeformableSurfaceEditReplayResult = NWB::Impl::DeformableSurfaceEditReplayResult;
using DeformableSurfaceEditUndoResult = NWB::Impl::DeformableSurfaceEditUndoResult;
using DeformableSurfaceEditRedoEntry = NWB::Impl::DeformableSurfaceEditRedoEntry;
using DeformableSurfaceEditHistory = NWB::Impl::DeformableSurfaceEditHistory;
using DeformableSurfaceEditRedoResult = NWB::Impl::DeformableSurfaceEditRedoResult;
using DeformableSurfaceEditHealResult = NWB::Impl::DeformableSurfaceEditHealResult;
using DeformableSurfaceEditResizeResult = NWB::Impl::DeformableSurfaceEditResizeResult;
using DeformableSurfaceEditMoveResult = NWB::Impl::DeformableSurfaceEditMoveResult;
using DeformableSurfaceEditPatchResult = NWB::Impl::DeformableSurfaceEditPatchResult;
using DeformableSurfaceEditLoopCutResult = NWB::Impl::DeformableSurfaceEditLoopCutResult;
using DeformableAccessoryAttachmentRecord = NWB::Impl::DeformableAccessoryAttachmentRecord;
using DeformableSurfaceEditDebugSnapshot = NWB::Impl::DeformableSurfaceEditDebugSnapshot;
using NWB::Impl::AttachAccessory;
using NWB::Impl::AttachAccessoryAtWallLoopParameter;
using NWB::Impl::ApplySurfaceEditState;
using NWB::Impl::BeginSurfaceEdit;
using NWB::Impl::BuildSurfaceEditStateDebugDump;
using NWB::Impl::BuildDeformableSurfaceEditDebugDump;
using NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot;
using NWB::Impl::BuildOperatorFootprintFromGeometry;
using NWB::Impl::BuildOperatorProfileFromGeometry;
using NWB::Impl::BuildOperatorShapeFromGeometry;
using NWB::Impl::BuildHolePreviewMesh;
using NWB::Impl::CommitHole;
using NWB::Impl::DeserializeSurfaceEditState;
using NWB::Impl::AddSurfaceEditLoopCut;
using NWB::Impl::HealSurfaceEdit;
using NWB::Impl::MoveSurfaceEdit;
using NWB::Impl::PatchSurfaceEdit;
using NWB::Impl::PreviewHole;
using NWB::Impl::RaycastVisibleDeformableRenderers;
using NWB::Impl::RedoLastSurfaceEdit;
using NWB::Impl::ResizeSurfaceEdit;
using NWB::Impl::ResolveAccessoryAttachmentTransform;
using NWB::Impl::SerializeSurfaceEditState;
using NWB::Impl::UndoLastSurfaceEdit;
namespace DeformableSurfaceEditRecordType = NWB::Impl::DeformableSurfaceEditRecordType;
namespace DeformableSurfaceEditPermission = NWB::Impl::DeformableSurfaceEditPermission;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
