// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/mesh_acceleration_state_policy.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_acceleration_update_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

[[nodiscard]] static ECSRenderDetail::MeshRayTracingResourceSnapshot AcceptedRuntimeGeometry()noexcept{
    ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
    mesh.runtimeMesh = true;
    mesh.runtimeGeometryContentRevision = 7u;
    mesh.blasGeometryContentRevision = 7u;
    mesh.swBvhGeometryContentRevision = 7u;
    mesh.blasBuildAccepted = true;
    mesh.swBvhBuildAccepted = true;
    mesh.swBvhTopologyBuilt = true;
    return mesh;
}

TEST(MeshAccelerationUpdateTests, UnknownRuntimeContentsNeverReuseAcceptedMeshAcceleration){
    auto mesh = AcceptedRuntimeGeometry();
    mesh.runtimeGeometryContentRevision = 0u;
    mesh.blasGeometryContentRevision = 0u;
    mesh.swBvhGeometryContentRevision = 0u;
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresBlasUpdate(mesh.blasBuildPending, mesh.blasBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.blasGeometryContentRevision));
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresSoftwareBvhUpdate(mesh.swBvhBuildPending, mesh.swBvhTopologyBuilt, mesh.swBvhBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.swBvhGeometryContentRevision));
}

TEST(MeshAccelerationUpdateTests, EachBackendTracksItsOwnAcceptedContentRevision){
    auto mesh = AcceptedRuntimeGeometry();
    ++mesh.runtimeGeometryContentRevision;
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresBlasUpdate(mesh.blasBuildPending, mesh.blasBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.blasGeometryContentRevision));
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresSoftwareBvhUpdate(mesh.swBvhBuildPending, mesh.swBvhTopologyBuilt, mesh.swBvhBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.swBvhGeometryContentRevision));
    mesh.blasGeometryContentRevision = mesh.runtimeGeometryContentRevision;
    EXPECT_FALSE(MeshAccelerationStatePolicy().requiresBlasUpdate(mesh.blasBuildPending, mesh.blasBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.blasGeometryContentRevision));
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresSoftwareBvhUpdate(mesh.swBvhBuildPending, mesh.swBvhTopologyBuilt, mesh.swBvhBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.swBvhGeometryContentRevision));
    mesh.swBvhGeometryContentRevision = mesh.runtimeGeometryContentRevision;
    EXPECT_FALSE(MeshAccelerationStatePolicy().requiresSoftwareBvhUpdate(mesh.swBvhBuildPending, mesh.swBvhTopologyBuilt, mesh.swBvhBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.swBvhGeometryContentRevision));
}

TEST(MeshAccelerationUpdateTests, DirtyStorageAndMissingTopologyOverrideMatchingContents){
    auto mesh = AcceptedRuntimeGeometry();
    mesh.blasBuildPending = true;
    mesh.swBvhBuildPending = true;
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresBlasUpdate(mesh.blasBuildPending, mesh.blasBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.blasGeometryContentRevision));
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresSoftwareBvhUpdate(mesh.swBvhBuildPending, mesh.swBvhTopologyBuilt, mesh.swBvhBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.swBvhGeometryContentRevision));
    mesh.swBvhBuildPending = false;
    mesh.swBvhTopologyBuilt = false;
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresSoftwareBvhUpdate(mesh.swBvhBuildPending, mesh.swBvhTopologyBuilt, mesh.swBvhBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.swBvhGeometryContentRevision));
}

TEST(MeshAccelerationUpdateTests, ImmutableStaticGeometryNeedsAcceptanceButNoRuntimeRevision){
    auto mesh = AcceptedRuntimeGeometry();
    mesh.runtimeMesh = false;
    mesh.runtimeGeometryContentRevision = 0u;
    EXPECT_FALSE(MeshAccelerationStatePolicy().requiresBlasUpdate(mesh.blasBuildPending, mesh.blasBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.blasGeometryContentRevision));
    EXPECT_FALSE(MeshAccelerationStatePolicy().requiresSoftwareBvhUpdate(mesh.swBvhBuildPending, mesh.swBvhTopologyBuilt, mesh.swBvhBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.swBvhGeometryContentRevision));
    mesh.blasBuildAccepted = false;
    mesh.swBvhBuildAccepted = false;
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresBlasUpdate(mesh.blasBuildPending, mesh.blasBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.blasGeometryContentRevision));
    EXPECT_TRUE(MeshAccelerationStatePolicy().requiresSoftwareBvhUpdate(mesh.swBvhBuildPending, mesh.swBvhTopologyBuilt, mesh.swBvhBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.swBvhGeometryContentRevision));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

