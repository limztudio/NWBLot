// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/mesh_acceleration_update.h>
#include <impl/ecs_render/mesh/mesh_system.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_acceleration_update_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

[[nodiscard]] static ECSRenderDetail::MeshRayTracingResourceSnapshot AcceptedRuntimeGeometry(){
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

TEST(MeshAccelerationUpdateTests, AcceptedUnchangedRuntimeGeometrySkipsBothMeshUpdates){
    const auto mesh = AcceptedRuntimeGeometry();
    EXPECT_FALSE(RequiresMeshBlasUpdate(mesh));
    EXPECT_FALSE(RequiresMeshSwBvhUpdate(mesh));
}

TEST(MeshAccelerationUpdateTests, UnknownRuntimeContentsNeverReuseAcceptedMeshAcceleration){
    auto mesh = AcceptedRuntimeGeometry();
    mesh.runtimeGeometryContentRevision = 0u;
    mesh.blasGeometryContentRevision = 0u;
    mesh.swBvhGeometryContentRevision = 0u;
    EXPECT_TRUE(RequiresMeshBlasUpdate(mesh));
    EXPECT_TRUE(RequiresMeshSwBvhUpdate(mesh));
}

TEST(MeshAccelerationUpdateTests, EachBackendTracksItsOwnAcceptedContentRevision){
    auto mesh = AcceptedRuntimeGeometry();
    ++mesh.runtimeGeometryContentRevision;
    EXPECT_TRUE(RequiresMeshBlasUpdate(mesh));
    EXPECT_TRUE(RequiresMeshSwBvhUpdate(mesh));
    mesh.blasGeometryContentRevision = mesh.runtimeGeometryContentRevision;
    EXPECT_FALSE(RequiresMeshBlasUpdate(mesh));
    EXPECT_TRUE(RequiresMeshSwBvhUpdate(mesh));
    mesh.swBvhGeometryContentRevision = mesh.runtimeGeometryContentRevision;
    EXPECT_FALSE(RequiresMeshSwBvhUpdate(mesh));
}

TEST(MeshAccelerationUpdateTests, RecordedButUnacceptedFirstBuildCannotBeReused){
    auto mesh = AcceptedRuntimeGeometry();
    // Direct recording can clear pending and produce topology before packet acceptance.
    mesh.blasBuildAccepted = false;
    mesh.swBvhBuildAccepted = false;
    EXPECT_TRUE(RequiresMeshBlasUpdate(mesh));
    EXPECT_TRUE(RequiresMeshSwBvhUpdate(mesh));
}

TEST(MeshAccelerationUpdateTests, DirtyStorageAndMissingTopologyOverrideMatchingContents){
    auto mesh = AcceptedRuntimeGeometry();
    mesh.blasBuildPending = true;
    mesh.swBvhBuildPending = true;
    EXPECT_TRUE(RequiresMeshBlasUpdate(mesh));
    EXPECT_TRUE(RequiresMeshSwBvhUpdate(mesh));
    mesh.swBvhBuildPending = false;
    mesh.swBvhTopologyBuilt = false;
    EXPECT_TRUE(RequiresMeshSwBvhUpdate(mesh));
}

TEST(MeshAccelerationUpdateTests, ImmutableStaticGeometryNeedsAcceptanceButNoRuntimeRevision){
    auto mesh = AcceptedRuntimeGeometry();
    mesh.runtimeMesh = false;
    mesh.runtimeGeometryContentRevision = 0u;
    EXPECT_FALSE(RequiresMeshBlasUpdate(mesh));
    EXPECT_FALSE(RequiresMeshSwBvhUpdate(mesh));
    mesh.blasBuildAccepted = false;
    mesh.swBvhBuildAccepted = false;
    EXPECT_TRUE(RequiresMeshBlasUpdate(mesh));
    EXPECT_TRUE(RequiresMeshSwBvhUpdate(mesh));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

