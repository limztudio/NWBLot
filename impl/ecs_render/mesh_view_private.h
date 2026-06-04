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
    Float4 worldToClip[4] = {
        Float4(1.f, 0.f, 0.f, 0.f),
        Float4(0.f, 1.f, 0.f, 0.f),
        Float4(0.f, 0.f, 1.f, 0.f),
        Float4(0.f, 0.f, 0.f, 1.f),
    };
    Float4 clipToWorld[4] = {
        Float4(1.f, 0.f, 0.f, 0.f),
        Float4(0.f, 1.f, 0.f, 0.f),
        Float4(0.f, 0.f, 1.f, 0.f),
        Float4(0.f, 0.f, 0.f, 1.f),
    };
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


inline SIMDVector BuildProjectedViewColumnVector(const SIMDVector viewColumn, const SIMDVector projection){
    const f32 viewZ = VectorGetZ(viewColumn);
    SIMDVector column = VectorMultiply(VectorSetW(viewColumn, viewZ), projection);
    column = VectorMultiplyAdd(VectorSet(0.0f, 0.0f, VectorGetW(viewColumn), 0.0f), VectorSplatW(projection), column);
    return VectorSetW(column, viewZ);
}

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

    SIMDMatrix viewBasis{};
    viewBasis.v[0] = right;
    viewBasis.v[1] = up;
    viewBasis.v[2] = forward;
    viewBasis.v[3] = s_SIMDIdentityR3;

    const SIMDMatrix viewColumns = MatrixTranspose(viewBasis);
    SIMDMatrix worldToClip{};
    worldToClip.v[0] = BuildProjectedViewColumnVector(viewColumns.v[0], projection);
    worldToClip.v[1] = BuildProjectedViewColumnVector(viewColumns.v[1], projection);
    worldToClip.v[2] = BuildProjectedViewColumnVector(viewColumns.v[2], projection);
    worldToClip.v[3] = BuildProjectedViewColumnVector(VectorSet(translationX, translationY, translationZ, 1.0f), projection);
    return worldToClip;
}

inline SIMDVector BuildViewFrustumPlaneVector(const SIMDVector normal, const SIMDVector point){
    return PlaneTests::FromPointNormal(normal, point, VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
}

inline SIMDVector BuildViewFrustumSidePlaneVector(
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
    for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex)
        StoreFloat(worldToClip.v[columnIndex], &state.worldToClip[columnIndex]);

    SIMDVector determinant;
    const SIMDMatrix clipToWorld = MatrixInverse(&determinant, worldToClip);
    if(IsFinite(VectorGetX(determinant)) && Abs(VectorGetX(determinant)) > 0.0f){
        for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex)
            StoreFloat(clipToWorld.v[columnIndex], &state.clipToWorld[columnIndex]);
    }

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

