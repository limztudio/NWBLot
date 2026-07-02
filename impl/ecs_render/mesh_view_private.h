// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"

#include <core/ecs/world.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/ecs_scene/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshViewGpuData{
    Float44 worldToClip = {};
    Float44 clipToWorld = {};
    Float4 cameraPosition = Float4(0.f, 0.f, 0.f, 1.f);
    Float4 frustumPlanes[NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT] = {
        Float4(1.f, 0.f, 0.f, 0.f),
        Float4(-1.f, 0.f, 0.f, 0.f),
        Float4(0.f, 1.f, 0.f, 0.f),
        Float4(0.f, -1.f, 0.f, 0.f),
        Float4(0.f, 0.f, 1.f, 0.f),
        Float4(0.f, 0.f, -1.f, 10000.f),
    };
};

static_assert(sizeof(MeshViewGpuData) == sizeof(f32) * NWB_MESH_VIEW_FLOAT_COUNT, "MeshViewGpuData layout must match the mesh shaders");
static_assert(alignof(MeshViewGpuData) >= alignof(Float4), "MeshViewGpuData must stay SIMD-aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshViewBufferName("ecs_render/mesh_view_data");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline SIMDMatrix BuildWorldToClipMatrix(
    const SIMDVector positionDepthBias,
    const SIMDVector right,
    const SIMDVector up,
    const SIMDVector forward,
    const SIMDVector projection
){
    const f32 translationX = -VectorGetX(Vector3Dot(positionDepthBias, right));
    const f32 translationY = -VectorGetX(Vector3Dot(positionDepthBias, up));
    const f32 translationZ = -VectorGetX(Vector3Dot(positionDepthBias, forward)) + VectorGetW(positionDepthBias);

    // World-to-view: basis vectors as rows, view-space translation in the fourth column (M*v form).
    SIMDMatrix worldToView{};
    worldToView.v[0] = VectorSetW(right, translationX);
    worldToView.v[1] = VectorSetW(up, translationY);
    worldToView.v[2] = VectorSetW(forward, translationZ);
    worldToView.v[3] = s_SIMDIdentityR3;

    // View-to-clip: projection lanes are x/y scale, z scale, and z bias; clip.w carries view-space z.
    SIMDMatrix viewToClip{};
    viewToClip.v[0] = VectorSet(VectorGetX(projection), 0.0f, 0.0f, 0.0f);
    viewToClip.v[1] = VectorSet(0.0f, VectorGetY(projection), 0.0f, 0.0f);
    viewToClip.v[2] = VectorSet(0.0f, 0.0f, VectorGetZ(projection), VectorGetW(projection));
    viewToClip.v[3] = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);

    return MatrixMultiply(viewToClip, worldToView);
}

NWB_INLINE SIMDVector BuildViewFrustumPlaneVector(const SIMDVector normal, const SIMDVector point){
    return PlaneTests::FromPointNormal(normal, point, VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
}

NWB_INLINE SIMDVector BuildViewFrustumSidePlaneVector(
    const SIMDVector forward,
    const SIMDVector side,
    const SIMDVector tanHalfAngle,
    const SIMDVector cameraPosition
){
    return BuildViewFrustumPlaneVector(
        VectorSetW(VectorMultiplyAdd(forward, tanHalfAngle, side), 0.0f),
        cameraPosition
    );
}

inline void BuildMeshViewFrustumVectors(
    SIMDVector& outCameraPosition,
    SIMDVector (&outFrustumPlanes)[NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT],
    const SIMDVector positionDepthBias,
    const SIMDVector right,
    const SIMDVector up,
    const SIMDVector forward,
    const SIMDVector tanHalfVertical,
    const SIMDVector tanHalfHorizontal,
    const SIMDVector nearPlane,
    const SIMDVector farPlane
){
    const SIMDVector cameraPosition = VectorSetW(
        VectorMultiplyAdd(VectorNegate(forward), VectorSplatW(positionDepthBias), positionDepthBias),
        1.0f
    );
    outCameraPosition = cameraPosition;

    outFrustumPlanes[0] = BuildViewFrustumSidePlaneVector(forward, right, tanHalfHorizontal, cameraPosition);
    outFrustumPlanes[1] = BuildViewFrustumSidePlaneVector(forward, VectorNegate(right), tanHalfHorizontal, cameraPosition);
    outFrustumPlanes[2] = BuildViewFrustumSidePlaneVector(forward, up, tanHalfVertical, cameraPosition);
    outFrustumPlanes[3] = BuildViewFrustumSidePlaneVector(forward, VectorNegate(up), tanHalfVertical, cameraPosition);

    const SIMDVector nearPoint = VectorSetW(
        VectorMultiplyAdd(forward, nearPlane, cameraPosition),
        0.0f
    );
    const SIMDVector farPoint = VectorSetW(
        VectorMultiplyAdd(forward, farPlane, cameraPosition),
        0.0f
    );
    outFrustumPlanes[4] = BuildViewFrustumPlaneVector(forward, nearPoint);
    outFrustumPlanes[5] = BuildViewFrustumPlaneVector(VectorNegate(forward), farPoint);
}

inline MeshViewGpuData ResolveMeshViewState(Core::ECS::World& world, const f32 fallbackAspectRatio){
    MeshViewGpuData state;
    NWB::Impl::Scene::SceneViewBasis viewBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    NWB::Impl::Scene::CameraComponent defaultCamera;
    NWB::Impl::Scene::CameraProjectionData projectionData;
    f32 nearPlane = defaultCamera.nearPlane();
    f32 farPlane = defaultCamera.farPlane();

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        viewBasis = NWB::Impl::Scene::BuildSceneViewBasis(*cameraView.transform);
        projectionData = cameraView.projectionData;
        nearPlane = cameraView.camera->nearPlane();
        farPlane = cameraView.camera->farPlane();
    }
    else if(!NWB::Impl::Scene::TryBuildCameraProjectionData(defaultCamera, fallbackAspectRatio, projectionData)){
        projectionData.aspectRatio = 1.0f;
        projectionData.tanHalfVerticalFov = 1.0f;
        projectionData.projectionParams = NWB::Impl::Scene::BuildDefaultCameraProjectionParams(fallbackAspectRatio);
    }

    const SIMDVector positionDepthBias = LoadFloat(viewBasis.positionDepthBias);
    const SIMDVector right = VectorSetW(LoadFloat(viewBasis.right), 0.0f);
    const SIMDVector up = VectorSetW(LoadFloat(viewBasis.up), 0.0f);
    const SIMDVector forward = VectorSetW(LoadFloat(viewBasis.forward), 0.0f);
    const SIMDVector projection = LoadFloat(projectionData.projectionParams);

    const SIMDMatrix worldToClip = BuildWorldToClipMatrix(positionDepthBias, right, up, forward, projection);
    StoreFloat(worldToClip, &state.worldToClip);

    SIMDVector determinant;
    const SIMDMatrix clipToWorld = MatrixInverse(&determinant, worldToClip);
    NWB_ASSERT(VectorIsFinite(determinant, 0xFu) && Vector4Greater(VectorAbs(determinant), VectorZero()));
    StoreFloat(clipToWorld, &state.clipToWorld);

    SIMDVector cameraPosition;
    SIMDVector frustumPlanes[NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT];
    BuildMeshViewFrustumVectors(
        cameraPosition,
        frustumPlanes,
        positionDepthBias,
        right,
        up,
        forward,
        VectorReplicate(projectionData.tanHalfVerticalFov),
        VectorReplicate(projectionData.tanHalfVerticalFov * projectionData.aspectRatio),
        VectorReplicate(nearPlane),
        VectorReplicate(farPlane)
    );
    StoreFloat(cameraPosition, &state.cameraPosition);
    for(usize planeIndex = 0u; planeIndex < NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT; ++planeIndex)
        StoreFloat(frustumPlanes[planeIndex], &state.frustumPlanes[planeIndex]);

    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

