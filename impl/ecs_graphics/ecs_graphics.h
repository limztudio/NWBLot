// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"
#include "deformable_debug_draw.h"
#include "deformable_picking.h"
#include "deformable_surface_edit.h"
#include "deformer_system.h"
#include "renderer_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSGraphics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using DeformableRendererComponent = NWB::Impl::DeformableRendererComponent;
using DeformableJointMatrix = NWB::Impl::DeformableJointMatrix;
using DeformableJointPaletteComponent = NWB::Impl::DeformableJointPaletteComponent;
using DeformableDisplacementComponent = NWB::Impl::DeformableDisplacementComponent;
using DeformableMorphWeight = NWB::Impl::DeformableMorphWeight;
using DeformableMorphWeightsComponent = NWB::Impl::DeformableMorphWeightsComponent;
using DeformablePickingInputs = NWB::Impl::DeformablePickingInputs;
using DeformablePickingRay = NWB::Impl::DeformablePickingRay;
using DeformablePosedHit = NWB::Impl::DeformablePosedHit;
using DeformableHoleEditParams = NWB::Impl::DeformableHoleEditParams;
using DeformableHoleEditResult = NWB::Impl::DeformableHoleEditResult;
using DeformableSurfaceEditSession = NWB::Impl::DeformableSurfaceEditSession;
using DeformableHolePreview = NWB::Impl::DeformableHolePreview;
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
using DeformableAccessoryAttachmentComponent = NWB::Impl::DeformableAccessoryAttachmentComponent;
using DeformableAccessoryAttachmentRecord = NWB::Impl::DeformableAccessoryAttachmentRecord;
using DeformableRuntimeMeshCache = NWB::Impl::DeformableRuntimeMeshCache;
using DeformableRuntimeMeshInstance = NWB::Impl::DeformableRuntimeMeshInstance;
using DeformableSurfaceEditDebugSnapshot = NWB::Impl::DeformableSurfaceEditDebugSnapshot;
using DeformerSystem = NWB::Impl::DeformerSystem;
using RendererComponent = NWB::Impl::RendererComponent;
using RendererSystem = NWB::Impl::RendererSystem;
using RuntimeMeshDirtyFlags = NWB::Impl::RuntimeMeshDirtyFlags;
using RuntimeMeshHandle = NWB::Impl::RuntimeMeshHandle;
using NWB::Impl::AttachAccessory;
using NWB::Impl::AttachAccessoryAtWallLoopParameter;
using NWB::Impl::ApplySurfaceEditState;
using NWB::Impl::BeginSurfaceEdit;
using NWB::Impl::BuildSurfaceEditStateDebugDump;
using NWB::Impl::BuildDeformableSurfaceEditDebugDump;
using NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot;
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
namespace RuntimeMeshDirtyFlag = NWB::Impl::RuntimeMeshDirtyFlag;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

