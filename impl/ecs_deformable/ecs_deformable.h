// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"
#include "deformable_runtime_mesh_cache.h"
#include "deformable_runtime_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSDeformable{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using DeformableRendererComponent = NWB::Impl::DeformableRendererComponent;
namespace GeometryClass = NWB::Impl::GeometryClass;
using DeformableJointMatrix = NWB::Impl::DeformableJointMatrix;
using NWB::Impl::MakeIdentityDeformableJointMatrix;
namespace DeformableSkinningMode = NWB::Impl::DeformableSkinningMode;
using DeformableJointPaletteComponent = NWB::Impl::DeformableJointPaletteComponent;
using DeformableSkeletonPoseComponent = NWB::Impl::DeformableSkeletonPoseComponent;
inline constexpr u32 s_DeformableSkeletonRootParent = NWB::Impl::s_DeformableSkeletonRootParent;
using DeformableDisplacementComponent = NWB::Impl::DeformableDisplacementComponent;
using DeformableMorphWeight = NWB::Impl::DeformableMorphWeight;
using DeformableMorphWeightsComponent = NWB::Impl::DeformableMorphWeightsComponent;
using DeformableSurfaceEditId = NWB::Impl::DeformableSurfaceEditId;
using DeformableAccessoryAttachmentComponent = NWB::Impl::DeformableAccessoryAttachmentComponent;
using DeformableRuntimeMeshCache = NWB::Impl::DeformableRuntimeMeshCache;
using DeformableRuntimeMeshInstance = NWB::Impl::DeformableRuntimeMeshInstance;
using RuntimeMeshDirtyFlags = NWB::Impl::RuntimeMeshDirtyFlags;
using RuntimeMeshHandle = NWB::Impl::RuntimeMeshHandle;
namespace RuntimeMeshDirtyFlag = NWB::Impl::RuntimeMeshDirtyFlag;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

