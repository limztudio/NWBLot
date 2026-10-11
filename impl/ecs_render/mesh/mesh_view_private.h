// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
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
        Float4(0.f, 0.f, -1.f, NWB::Impl::Scene::CameraDefaults::s_FarPlane),
    };
};

struct MeshViewBufferUpload{
    MeshViewGpuData viewState;
    bool uploadRequired = false;
};

static_assert(sizeof(MeshViewGpuData) == sizeof(f32) * NWB_MESH_VIEW_FLOAT_COUNT, "MeshViewGpuData layout must match the mesh shaders");
static_assert(alignof(MeshViewGpuData) >= alignof(Float4), "MeshViewGpuData must stay SIMD-aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshViewBufferName("ecs_render/mesh_view_data");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline SIMDMatrix BuildWorldToClipMatrix(
    const SIMDVectorSingle positionDepthBias,
    const SIMDVectorSingle right,
    const SIMDVectorSingle up,
    const SIMDVectorSingle forward,
    const SIMDVectorSingle projection
)noexcept{
    const SIMDVectorSingle translation = VectorAdd(
        VectorNegate(VectorMergeX(
            Vector3Dot(positionDepthBias, right),
            Vector3Dot(positionDepthBias, up),
            Vector3Dot(positionDepthBias, forward),
            VectorZero()
        )),
        VectorAndInt(VectorSplatW(positionDepthBias), s_SIMDMaskZ)
    );

    // World-to-view: basis rows, translation in fourth column.
    SIMDMatrix worldToView{};
    worldToView.v[0] = VectorSelect(right, VectorSplatX(translation), s_SIMDMaskW);
    worldToView.v[1] = VectorSelect(up, VectorSplatY(translation), s_SIMDMaskW);
    worldToView.v[2] = VectorSelect(forward, VectorSplatZ(translation), s_SIMDMaskW);
    worldToView.v[3] = s_SIMDIdentityR3;

    // View-to-clip: x/y scale, z scale/bias; clip.w carries view-space z.
    SIMDMatrix viewToClip{};
    viewToClip.v[0] = VectorAndInt(projection, s_SIMDMaskX);
    viewToClip.v[1] = VectorAndInt(projection, s_SIMDMaskY);
    viewToClip.v[2] = VectorSelect(VectorAndInt(projection, s_SIMDMaskZ), VectorSplatW(projection), s_SIMDMaskW);
    viewToClip.v[3] = s_SIMDIdentityR2;

    return MatrixMultiply(viewToClip, worldToView);
}

NWB_INLINE SIMDVectorSingle BuildViewFrustumPlaneVector(const SIMDVectorSingle normal, const SIMDVectorSingle point){
    return PlaneTests::FromPointNormal(normal, point, VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
}

NWB_INLINE SIMDVectorSingle BuildViewFrustumSidePlaneVector(
    const SIMDVectorSingle forward,
    const SIMDVectorSingle side,
    const SIMDVectorSingle tanHalfAngle,
    const SIMDVectorSingle cameraPosition
){
    return BuildViewFrustumPlaneVector(
        VectorSetW(VectorMultiplyAdd(forward, tanHalfAngle, side), 0.0f),
        cameraPosition
    );
}

inline void BuildMeshViewFrustumVectors(
    SIMDVectorSingle& outCameraPosition,
    SIMDVectorSingle (&outFrustumPlanes)[NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT],
    const SIMDVectorSingle positionDepthBias,
    const SIMDVectorSingle right,
    const SIMDVectorSingle up,
    const SIMDVectorSingle forward,
    const SIMDVectorSingle tanHalfVertical,
    const SIMDVectorSingle tanHalfHorizontal,
    const SIMDVectorSingle nearPlane,
    const SIMDVectorSingle farPlane
){
    const SIMDVectorSingle cameraPosition = VectorSetW(
        VectorMultiplyAdd(VectorNegate(forward), VectorSplatW(positionDepthBias), positionDepthBias),
        1.0f
    );
    outCameraPosition = cameraPosition;

    outFrustumPlanes[0] = BuildViewFrustumSidePlaneVector(forward, right, tanHalfHorizontal, cameraPosition);
    outFrustumPlanes[1] = BuildViewFrustumSidePlaneVector(forward, VectorNegate(right), tanHalfHorizontal, cameraPosition);
    outFrustumPlanes[2] = BuildViewFrustumSidePlaneVector(forward, up, tanHalfVertical, cameraPosition);
    outFrustumPlanes[3] = BuildViewFrustumSidePlaneVector(forward, VectorNegate(up), tanHalfVertical, cameraPosition);

    const SIMDVectorSingle nearPoint = VectorSetW(
        VectorMultiplyAdd(forward, nearPlane, cameraPosition),
        0.0f
    );
    const SIMDVectorSingle farPoint = VectorSetW(
        VectorMultiplyAdd(forward, farPlane, cameraPosition),
        0.0f
    );
    outFrustumPlanes[4] = BuildViewFrustumPlaneVector(forward, nearPoint);
    outFrustumPlanes[5] = BuildViewFrustumPlaneVector(VectorNegate(forward), farPoint);
}

inline MeshViewGpuData ResolveMeshViewState(Core::ECS::World& world, const f32 fallbackAspectRatio){
    MeshViewGpuData state;
    NWB::Impl::Scene::SceneViewBasis viewBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();
    NWB::Impl::Scene::CameraProjection projection = NWB::Impl::Scene::BuildDefaultCameraProjection(fallbackAspectRatio);

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        viewBasis = NWB::Impl::Scene::BuildSceneViewBasis(
            LoadFloat(cameraView.transform->position),
            LoadFloat(cameraView.transform->rotation)
        );
        projection = cameraView.projection;
    }

    const SIMDVectorSingle positionDepthBias = LoadFloat(viewBasis.positionDepthBias);
    const SIMDVectorSingle right = VectorSetW(LoadFloat(viewBasis.right), 0.0f);
    const SIMDVectorSingle up = VectorSetW(LoadFloat(viewBasis.up), 0.0f);
    const SIMDVectorSingle forward = VectorSetW(LoadFloat(viewBasis.forward), 0.0f);
    const SIMDVectorSingle projectionParams = LoadFloat(projection.projectionParams);
    const SIMDVectorSingle tanHalfVerticalFov = VectorReplicate(projection.tanHalfVerticalFov);
    const SIMDVectorSingle aspectRatio = VectorReplicate(projection.aspectRatio);
    const SIMDVectorSingle nearPlane = VectorReplicate(projection.nearPlane);
    const SIMDVectorSingle farPlane = VectorReplicate(projection.farPlane);

    const SIMDMatrix worldToClip = BuildWorldToClipMatrix(positionDepthBias, right, up, forward, projectionParams);
    StoreFloat(worldToClip, state.worldToClip);

    SIMDVectorSingle determinant;
    const SIMDMatrix clipToWorld = MatrixInverse(&determinant, worldToClip);
    NWB_ASSERT(VectorIsFinite(determinant, VectorComponentMask::s_XYZW) && Vector4Greater(VectorAbs(determinant), VectorZero()));
    StoreFloat(clipToWorld, state.clipToWorld);

    SIMDVectorSingle cameraPosition;
    SIMDVectorSingle frustumPlanes[NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT];
    BuildMeshViewFrustumVectors(
        cameraPosition,
        frustumPlanes,
        positionDepthBias,
        right,
        up,
        forward,
        tanHalfVerticalFov,
        VectorMultiply(tanHalfVerticalFov, aspectRatio),
        nearPlane,
        farPlane
    );
    StoreFloat(cameraPosition, state.cameraPosition);
    for(usize planeIndex = 0u; planeIndex < NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT; ++planeIndex)
        StoreFloat(frustumPlanes[planeIndex], state.frustumPlanes[planeIndex]);

    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

