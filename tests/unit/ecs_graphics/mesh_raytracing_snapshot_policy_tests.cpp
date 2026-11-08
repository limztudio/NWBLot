// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/mesh/mesh_raytracing_snapshot_policy.h>
#include <impl/ecs_render/mesh/mesh_system.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_raytracing_snapshot_policy_tests{

using namespace NWB::Impl;

TEST(MeshRayTracingSnapshotPolicyTests, MissingMeshletBoundsDoNotPublishPartialSnapshot){
    MeshResources mesh;
    mesh.runtimeMesh = true;
    mesh.runtimeGeometryContentRevision = 7u;
    mesh.meshletCount = 1u;
    auto snapshot = MeshRayTracingSnapshotPolicy::capture(mesh);
    EXPECT_EQ(snapshot.meshletCount, 0u);
    EXPECT_TRUE(MeshRayTracingSnapshotPolicy::matches(mesh, snapshot));
    snapshot.meshletCount = 1u;
    EXPECT_FALSE(MeshRayTracingSnapshotPolicy::matches(mesh, snapshot));
}

TEST(MeshRayTracingSnapshotPolicyTests, IdentityIgnoresBuildStateButTracksGeometryRevision){
    MeshResources mesh;
    mesh.runtimeMesh = true;
    mesh.runtimeGeometryContentRevision = 7u;
    const auto current = MeshRayTracingSnapshotPolicy::capture(mesh);
    auto other = current;
    other.blasBuildPending = !current.blasBuildPending;
    EXPECT_TRUE(MeshRayTracingSnapshotPolicy::sameIdentity(current, other));
    ++other.runtimeGeometryContentRevision;
    EXPECT_FALSE(MeshRayTracingSnapshotPolicy::sameIdentity(current, other));
}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
