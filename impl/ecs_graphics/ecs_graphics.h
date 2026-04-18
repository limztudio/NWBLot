// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"
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
using DeformableRuntimeMeshCache = NWB::Impl::DeformableRuntimeMeshCache;
using DeformableRuntimeMeshInstance = NWB::Impl::DeformableRuntimeMeshInstance;
using DeformerSystem = NWB::Impl::DeformerSystem;
using RendererComponent = NWB::Impl::RendererComponent;
using RendererSystem = NWB::Impl::RendererSystem;
using RuntimeMeshDirtyFlags = NWB::Impl::RuntimeMeshDirtyFlags;
using RuntimeMeshHandle = NWB::Impl::RuntimeMeshHandle;
using NWB::Impl::BuildDeformablePickingVertices;
using NWB::Impl::CommitDeformableRestSpaceHole;
using NWB::Impl::RaycastDeformableRuntimeMesh;
using NWB::Impl::RaycastVisibleDeformableRenderers;
using NWB::Impl::ResolveDeformableRestSurfaceSample;
namespace RuntimeMeshDirtyFlag = NWB::Impl::RuntimeMeshDirtyFlag;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

