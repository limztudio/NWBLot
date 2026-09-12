// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_shadow_upload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, MergesDeferredPreflightUploadsIntoShadowPreparePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId currentBindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/deferred_bindless_slots"),
        "Deferred Bindless Slots",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/raytrace_material_context_slots"),
        "Ray-Trace Material Context Slots",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId causticEmissionTargets = AddBufferMetadata(
        graph,
        Name("tests/task_graph/caustic_emission_targets"),
        "Caustic Emission Targets",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId surfelFrameConstants = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_frame_constants"),
        "Surfel Frame Constants",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId shadowInstanceMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_instance_materials"),
        "Shadow Instance Materials",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId shadowInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_instances"),
        "Shadow Instances",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId shadowMaterialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_material_typed"),
        "Shadow Typed Materials",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId sceneBvhNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/scene_bvh_nodes"),
        "Scene BVH Nodes",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId sceneBvhInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/scene_bvh_instances"),
        "Scene BVH Instances",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId swBvhParent = AddBufferMetadata(
        graph,
        Name("tests/task_graph/sw_bvh_parent"),
        "Software BVH Parent",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId swBvhSortKeys = AddBufferMetadata(
        graph,
        Name("tests/task_graph/sw_bvh_sort_keys"),
        "Software BVH Sort Keys",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId swBvhSortPayload = AddBufferMetadata(
        graph,
        Name("tests/task_graph/sw_bvh_sort_payload"),
        "Software BVH Sort Payload",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId swBvhVisitCounter = AddBufferMetadata(
        graph,
        Name("tests/task_graph/sw_bvh_visit_counter"),
        "Software BVH Visit Counter",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId sceneTlas = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/scene_tlas"),
        "Scene TLAS",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId sceneTlasBacking = AddBufferMetadata(
        graph,
        Name("tests/task_graph/scene_tlas_backing"),
        "Scene TLAS Backing",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasA = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_a"),
        "Mesh BLAS A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasABacking = AddBufferMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_a_backing"),
        "Mesh BLAS A Backing",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasAPosition = AddBufferMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_a_position"),
        "Mesh BLAS A Position",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasAIndex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_a_index"),
        "Mesh BLAS A Index",
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasB = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_b"),
        "Mesh BLAS B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasBBacking = AddBufferMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_b_backing"),
        "Mesh BLAS B Backing",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(causticEmissionTargets.valid());
    ASSERT_TRUE(surfelFrameConstants.valid());
    ASSERT_TRUE(shadowInstanceMaterials.valid());
    ASSERT_TRUE(shadowInstances.valid());
    ASSERT_TRUE(shadowMaterialTyped.valid());
    ASSERT_TRUE(sceneBvhNodes.valid());
    ASSERT_TRUE(sceneBvhInstances.valid());
    ASSERT_TRUE(swBvhParent.valid());
    ASSERT_TRUE(swBvhSortKeys.valid());
    ASSERT_TRUE(swBvhSortPayload.valid());
    ASSERT_TRUE(swBvhVisitCounter.valid());
    const Graphics::GpuGraphResourceId softwareBvhBuildStateMembers[] = {
        swBvhParent,
        swBvhSortKeys,
        swBvhSortPayload,
        swBvhVisitCounter,
    };
    const Graphics::GpuGraphResourceSetId softwareBvhBuildStateSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/shadow_prepare_software_bvh_build_state"))
            .setMarkerLabel("Shadow Prepare Software BVH Build State")
            .setMembers(softwareBvhBuildStateMembers, LengthOf(softwareBvhBuildStateMembers))
    );
    ASSERT_TRUE(softwareBvhBuildStateSet.valid());
    ASSERT_TRUE(sceneTlas.valid());
    ASSERT_TRUE(sceneTlasBacking.valid());
    ASSERT_TRUE(meshBlasA.valid());
    ASSERT_TRUE(meshBlasABacking.valid());
    ASSERT_TRUE(meshBlasAPosition.valid());
    ASSERT_TRUE(meshBlasAIndex.valid());
    ASSERT_TRUE(meshBlasB.valid());
    ASSERT_TRUE(meshBlasBBacking.valid());
    const Graphics::GpuGraphResourceId meshBlasGeometryBuildInputMembers[] = {
        meshBlasAPosition,
        meshBlasAIndex,
    };
    const Graphics::GpuGraphResourceSetId meshBlasGeometryBuildInputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/shadow_prepare_blas_geometry_build_inputs"))
            .setMarkerLabel("Shadow Prepare BLAS Geometry Build Inputs")
            .setMembers(meshBlasGeometryBuildInputMembers, LengthOf(meshBlasGeometryBuildInputMembers))
    );
    ASSERT_TRUE(meshBlasGeometryBuildInputSet.valid());
    const Graphics::GpuGraphResourceId hybridSoftwareTailInputMembers[] = {
        meshBlasAPosition,
        meshBlasAIndex,
    };
    const Graphics::GpuGraphResourceSetId hybridSoftwareTailInputSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/shadow_prepare_hybrid_software_tail_inputs"))
            .setMarkerLabel("Shadow Prepare Hybrid Software Tail Inputs")
            .setMembers(hybridSoftwareTailInputMembers, LengthOf(hybridSoftwareTailInputMembers))
    );
    ASSERT_TRUE(hybridSoftwareTailInputSet.valid());
    const Graphics::GpuGraphResourceId accelStructFinalizeMembers[] = {
        sceneTlas,
        sceneTlasBacking,
        meshBlasA,
        meshBlasABacking,
    };
    const Graphics::GpuGraphResourceSetId accelStructFinalizeSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/shadow_prepare_accel_struct_finalize_resources"))
            .setMarkerLabel("Shadow Prepare Accel-Struct Finalize Resources")
            .setMembers(accelStructFinalizeMembers, LengthOf(accelStructFinalizeMembers))
    );
    ASSERT_TRUE(accelStructFinalizeSet.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsUploadRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    uploadScheduling.forceSubmissionBoundary = false;
    uploadScheduling.allowPacketMerge = true;
    Graphics::GpuTaskSchedulingHint shadowPrepareScheduling;
    shadowPrepareScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowPrepareScheduling.forceSubmissionBoundary = false;
    shadowPrepareScheduling.allowPacketMerge = true;
    shadowPrepareScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse uploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            // Keep-initial-state selector buffers publish Common after their built-in copy. The first native
            // consumer transitions it to ConstantBuffer and owns the following cross-queue state handoff.
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_slots_upload"))
        .setMarkerLabel("Deferred Bindless Slots Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(uploadScheduling)
        .setResourceUses(uploadUses, LengthOf(uploadUses))
    ;
    const Graphics::GpuTaskId upload = graph.addTask(uploadDesc);
    ASSERT_TRUE(upload.valid());

    const Graphics::GpuTaskResourceUse materialContextUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint materialContextUploadScheduling = uploadScheduling;
    materialContextUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc materialContextUploadDesc;
    materialContextUploadDesc
        .setIdentity(Name("tests/task_graph/raytrace_material_context_slots_upload"))
        .setMarkerLabel("Ray-Trace Material Context Slots Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(materialContextUploadScheduling)
        .setDependencies(&upload, 1u)
        .setResourceUses(materialContextUploadUses, LengthOf(materialContextUploadUses))
    ;
    const Graphics::GpuTaskId materialContextUpload = graph.addTask(materialContextUploadDesc);
    ASSERT_TRUE(materialContextUpload.valid());

    const Graphics::GpuTaskResourceUse causticEmissionTargetsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = causticEmissionTargets,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint causticEmissionTargetsUploadScheduling = uploadScheduling;
    causticEmissionTargetsUploadScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    causticEmissionTargetsUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc causticEmissionTargetsUploadDesc;
    causticEmissionTargetsUploadDesc
        .setIdentity(Name("tests/task_graph/caustic_emission_targets_upload"))
        .setMarkerLabel("Caustic Emission Targets Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(causticEmissionTargetsUploadScheduling)
        .setDependencies(&materialContextUpload, 1u)
        .setResourceUses(causticEmissionTargetsUploadUses, LengthOf(causticEmissionTargetsUploadUses))
    ;
    const Graphics::GpuTaskId causticEmissionTargetsUpload = graph.addTask(causticEmissionTargetsUploadDesc);
    ASSERT_TRUE(causticEmissionTargetsUpload.valid());

    const Graphics::GpuTaskResourceUse surfelFrameConstantsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = surfelFrameConstants,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint surfelFrameConstantsUploadScheduling = uploadScheduling;
    surfelFrameConstantsUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc surfelFrameConstantsUploadDesc;
    surfelFrameConstantsUploadDesc
        .setIdentity(Name("tests/task_graph/surfel_frame_constants_upload"))
        .setMarkerLabel("Surfel Frame Constants Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(surfelFrameConstantsUploadScheduling)
        .setDependencies(&causticEmissionTargetsUpload, 1u)
        .setResourceUses(surfelFrameConstantsUploadUses, LengthOf(surfelFrameConstantsUploadUses))
    ;
    const Graphics::GpuTaskId surfelFrameConstantsUpload = graph.addTask(surfelFrameConstantsUploadDesc);
    ASSERT_TRUE(surfelFrameConstantsUpload.valid());

    const Graphics::GpuTaskResourceUse shadowInstanceMaterialsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstanceMaterials,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint shadowMaterialContextUploadScheduling = uploadScheduling;
    shadowMaterialContextUploadScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    shadowMaterialContextUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc shadowInstanceMaterialsUploadDesc;
    shadowInstanceMaterialsUploadDesc
        .setIdentity(Name("tests/task_graph/shadow_instance_materials_upload"))
        .setMarkerLabel("Shadow Instance Materials Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(shadowMaterialContextUploadScheduling)
        .setDependencies(&surfelFrameConstantsUpload, 1u)
        .setResourceUses(shadowInstanceMaterialsUploadUses, LengthOf(shadowInstanceMaterialsUploadUses))
    ;
    const Graphics::GpuTaskId shadowInstanceMaterialsUpload = graph.addTask(shadowInstanceMaterialsUploadDesc);
    ASSERT_TRUE(shadowInstanceMaterialsUpload.valid());

    const Graphics::GpuTaskResourceUse shadowInstancesUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowInstancesUploadDesc;
    shadowInstancesUploadDesc
        .setIdentity(Name("tests/task_graph/shadow_instances_upload"))
        .setMarkerLabel("Shadow Instances Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(shadowMaterialContextUploadScheduling)
        .setDependencies(&shadowInstanceMaterialsUpload, 1u)
        .setResourceUses(shadowInstancesUploadUses, LengthOf(shadowInstancesUploadUses))
    ;
    const Graphics::GpuTaskId shadowInstancesUpload = graph.addTask(shadowInstancesUploadDesc);
    ASSERT_TRUE(shadowInstancesUpload.valid());

    const Graphics::GpuTaskResourceUse shadowMaterialTypedUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = shadowMaterialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowMaterialTypedUploadDesc;
    shadowMaterialTypedUploadDesc
        .setIdentity(Name("tests/task_graph/shadow_material_typed_upload"))
        .setMarkerLabel("Shadow Typed Materials Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(shadowMaterialContextUploadScheduling)
        .setDependencies(&shadowInstancesUpload, 1u)
        .setResourceUses(shadowMaterialTypedUploadUses, LengthOf(shadowMaterialTypedUploadUses))
    ;
    const Graphics::GpuTaskId shadowMaterialTypedUpload = graph.addTask(shadowMaterialTypedUploadDesc);
    ASSERT_TRUE(shadowMaterialTypedUpload.valid());

    const Graphics::GpuTaskResourceUse sceneBvhNodesUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhNodes,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint sceneBvhUploadScheduling = uploadScheduling;
    sceneBvhUploadScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    sceneBvhUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc sceneBvhNodesUploadDesc;
    sceneBvhNodesUploadDesc
        .setIdentity(Name("tests/task_graph/scene_bvh_nodes_upload"))
        .setMarkerLabel("Scene BVH Nodes Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(sceneBvhUploadScheduling)
        .setDependencies(&shadowMaterialTypedUpload, 1u)
        .setResourceUses(sceneBvhNodesUploadUses, LengthOf(sceneBvhNodesUploadUses))
    ;
    const Graphics::GpuTaskId sceneBvhNodesUpload = graph.addTask(sceneBvhNodesUploadDesc);
    ASSERT_TRUE(sceneBvhNodesUpload.valid());

    const Graphics::GpuTaskResourceUse sceneBvhInstancesUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc sceneBvhInstancesUploadDesc;
    sceneBvhInstancesUploadDesc
        .setIdentity(Name("tests/task_graph/scene_bvh_instances_upload"))
        .setMarkerLabel("Scene BVH Instances Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(sceneBvhUploadScheduling)
        .setDependencies(&sceneBvhNodesUpload, 1u)
        .setResourceUses(sceneBvhInstancesUploadUses, LengthOf(sceneBvhInstancesUploadUses))
    ;
    const Graphics::GpuTaskId sceneBvhInstancesUpload = graph.addTask(sceneBvhInstancesUploadDesc);
    ASSERT_TRUE(sceneBvhInstancesUpload.valid());

    const Graphics::GpuTaskResourceSetUse shadowPrepareResourceSetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = meshBlasGeometryBuildInputSet,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructBuildInput,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = softwareBvhBuildStateSet,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };

    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = causticEmissionTargets,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = surfelFrameConstants,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstanceMaterials,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowMaterialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhNodes,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        // The frozen native TLAS recorder consumes the graph-owned build state. Its adjacent state-only finalizer
        // below publishes the descriptor-visible Read handoff before the Compute consumer can begin.
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlas,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructWrite,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlasBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructWrite,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        // The frozen BLAS build records with graph-owned typed/backing Write aliases. Its frozen shared geometry
        // enters through the immutable build-input set; the adjacent hybrid tail lowers it to SRV before SW-BVH
        // work records.
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasA,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructWrite,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasABacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructWrite,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        // A state-only retained BLAS continues to seed direct/native compatibility routes without adding a packet.
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasB,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasBBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(shadowPrepareScheduling)
        .setDependencies(&sceneBvhInstancesUpload, 1u)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
        .setResourceSetUses(shadowPrepareResourceSetUses, LengthOf(shadowPrepareResourceSetUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());

    // The hybrid HW-to-SW continuation records real compatibility work after the hardware build, but it keeps the
    // original accepting packet whole so the opaque fallback and its persistent state handoff stay atomic. Its
    // frozen BLAS inputs become graph-owned ShaderResource reads at this callback boundary.
    Graphics::GpuTaskSchedulingHint shadowPrepareHybridTailScheduling;
    shadowPrepareHybridTailScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowPrepareHybridTailScheduling.forceSubmissionBoundary = false;
    shadowPrepareHybridTailScheduling.allowPacketMerge = true;
    shadowPrepareHybridTailScheduling.mergeWithPrevious = true;
    shadowPrepareHybridTailScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceSetUse hybridSoftwareTailInputSetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = hybridSoftwareTailInputSet,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareHybridTailDesc;
    shadowPrepareHybridTailDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_shadow_prepare_hybrid_tail"))
        .setMarkerLabel("Shadow Preparation Hybrid Software Tail")
        .setQueue(graphicsRequest)
        .setScheduling(shadowPrepareHybridTailScheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceSetUses(hybridSoftwareTailInputSetUses, LengthOf(hybridSoftwareTailInputSetUses))
    ;
    const Graphics::GpuTaskId shadowPrepareHybridTail = graph.addTask(shadowPrepareHybridTailDesc);
    ASSERT_TRUE(shadowPrepareHybridTail.valid());

    const Graphics::GpuTaskResourceSetUse accelStructFinalizeSetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = accelStructFinalizeSet,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskSchedulingHint shadowPrepareTlasFinalizeScheduling;
    shadowPrepareTlasFinalizeScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    shadowPrepareTlasFinalizeScheduling.forceSubmissionBoundary = false;
    shadowPrepareTlasFinalizeScheduling.allowPacketMerge = true;
    shadowPrepareTlasFinalizeScheduling.mergeWithPrevious = true;
    // Shadow Preparation still directly signals retained descriptor resources to Compute. The finalizer follows the
    // hybrid tail and explicitly retains its TLAS/BLAS Read handoffs in that accepting Graphics packet.
    shadowPrepareTlasFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Graphics::GpuTaskDesc shadowPrepareTlasFinalizeDesc;
    shadowPrepareTlasFinalizeDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_shadow_prepare_tlas_finalize"))
        .setMarkerLabel("Shadow Preparation TLAS Finalize")
        .setQueue(graphicsRequest)
        .setScheduling(shadowPrepareTlasFinalizeScheduling)
        .setDependencies(&shadowPrepareHybridTail, 1u)
        .setResourceSetUses(accelStructFinalizeSetUses, LengthOf(accelStructFinalizeSetUses))
    ;
    const Graphics::GpuTaskId shadowPrepareTlasFinalize = graph.addTask(shadowPrepareTlasFinalizeDesc);
    ASSERT_TRUE(shadowPrepareTlasFinalize.valid());

    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = causticEmissionTargets,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = surfelFrameConstants,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstanceMaterials,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowMaterialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhNodes,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlas,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlasBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&shadowPrepareTlasFinalize, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibility = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibility.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);

    // Shadow Preparation retains direct cross-queue descriptor hazards even though Visibility explicitly depends on
    // the finalizer. The hybrid tail and finalizer's opt-ins must keep the complete callback chain in the first
    // accepting Graphics packet.
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        shadowPrepare,
        shadowVisibility,
        currentBindlessSlots,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        shadowPrepare,
        shadowPrepareHybridTail,
        meshBlasAPosition,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        shadowPrepare,
        shadowPrepareHybridTail,
        meshBlasAIndex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuSubmissionPacketId uploadPacket = compiledPlan.packetForTask(upload);
    const Graphics::GpuSubmissionPacketId materialContextUploadPacket = compiledPlan.packetForTask(materialContextUpload);
    const Graphics::GpuSubmissionPacketId causticEmissionTargetsUploadPacket = compiledPlan.packetForTask(
        causticEmissionTargetsUpload
    );
    const Graphics::GpuSubmissionPacketId surfelFrameConstantsUploadPacket = compiledPlan.packetForTask(
        surfelFrameConstantsUpload
    );
    const Graphics::GpuSubmissionPacketId shadowInstanceMaterialsUploadPacket = compiledPlan.packetForTask(
        shadowInstanceMaterialsUpload
    );
    const Graphics::GpuSubmissionPacketId shadowInstancesUploadPacket = compiledPlan.packetForTask(
        shadowInstancesUpload
    );
    const Graphics::GpuSubmissionPacketId shadowMaterialTypedUploadPacket = compiledPlan.packetForTask(
        shadowMaterialTypedUpload
    );
    const Graphics::GpuSubmissionPacketId sceneBvhNodesUploadPacket = compiledPlan.packetForTask(sceneBvhNodesUpload);
    const Graphics::GpuSubmissionPacketId sceneBvhInstancesUploadPacket = compiledPlan.packetForTask(
        sceneBvhInstancesUpload
    );
    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledPlan.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId shadowPrepareHybridTailPacket = compiledPlan.packetForTask(
        shadowPrepareHybridTail
    );
    const Graphics::GpuSubmissionPacketId shadowPrepareTlasFinalizePacket = compiledPlan.packetForTask(
        shadowPrepareTlasFinalize
    );
    const Graphics::GpuSubmissionPacketId shadowVisibilityPacket = compiledPlan.packetForTask(shadowVisibility);
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(materialContextUploadPacket.valid());
    ASSERT_TRUE(causticEmissionTargetsUploadPacket.valid());
    ASSERT_TRUE(surfelFrameConstantsUploadPacket.valid());
    ASSERT_TRUE(shadowInstanceMaterialsUploadPacket.valid());
    ASSERT_TRUE(shadowInstancesUploadPacket.valid());
    ASSERT_TRUE(shadowMaterialTypedUploadPacket.valid());
    ASSERT_TRUE(sceneBvhNodesUploadPacket.valid());
    ASSERT_TRUE(sceneBvhInstancesUploadPacket.valid());
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(shadowPrepareHybridTailPacket.valid());
    ASSERT_TRUE(shadowPrepareTlasFinalizePacket.valid());
    ASSERT_TRUE(shadowVisibilityPacket.valid());
    EXPECT_EQ(uploadPacket, shadowPreparePacket);
    EXPECT_EQ(materialContextUploadPacket, shadowPreparePacket);
    EXPECT_EQ(causticEmissionTargetsUploadPacket, shadowPreparePacket);
    EXPECT_EQ(surfelFrameConstantsUploadPacket, shadowPreparePacket);
    EXPECT_EQ(shadowInstanceMaterialsUploadPacket, shadowPreparePacket);
    EXPECT_EQ(shadowInstancesUploadPacket, shadowPreparePacket);
    EXPECT_EQ(shadowMaterialTypedUploadPacket, shadowPreparePacket);
    EXPECT_EQ(sceneBvhNodesUploadPacket, shadowPreparePacket);
    EXPECT_EQ(sceneBvhInstancesUploadPacket, shadowPreparePacket);
    EXPECT_EQ(shadowPrepareHybridTailPacket, shadowPreparePacket);
    EXPECT_EQ(shadowPrepareTlasFinalizePacket, shadowPreparePacket);
    EXPECT_NE(shadowPreparePacket, shadowVisibilityPacket);
    EXPECT_EQ(compiledPlan.packet(shadowPreparePacket).plan->taskCount, 12u);
    const Graphics::GpuTaskId* const shadowPrepareTasks = compiledPlan.packet(shadowPreparePacket).tasks;
    ASSERT_NE(shadowPrepareTasks, nullptr);
    EXPECT_EQ(shadowPrepareTasks[9u], shadowPrepare);
    EXPECT_EQ(shadowPrepareTasks[10u], shadowPrepareHybridTail);
    EXPECT_EQ(shadowPrepareTasks[11u], shadowPrepareTlasFinalize);
    const Graphics::GpuSubmissionPacketRange shadowPrepareRange = compiledPlan.packetRange(
        shadowPreparePacket,
        shadowPreparePacket
    );
    ASSERT_TRUE(shadowPrepareRange.valid());
    EXPECT_EQ(shadowPrepareRange.packetCount, 1u);
    const Graphics::GpuCompiledTask* const compiledShadowPrepare = compiledPlan.findTask(shadowPrepare).plan;
    const Graphics::GpuCompiledTask* const compiledShadowPrepareHybridTail = compiledPlan.findTask(
        shadowPrepareHybridTail
    ).plan;
    const Graphics::GpuCompiledTask* const compiledShadowPrepareTlasFinalize = compiledPlan.findTask(
        shadowPrepareTlasFinalize
    ).plan;
    ASSERT_NE(compiledShadowPrepare, nullptr);
    ASSERT_NE(compiledShadowPrepareHybridTail, nullptr);
    ASSERT_NE(compiledShadowPrepareTlasFinalize, nullptr);
    const Graphics::GpuCompiledBarrier* const shadowPrepareBarriers = compiledPlan.findTask(shadowPrepare).prologueBarriers;
    ASSERT_NE(shadowPrepareBarriers, nullptr);
    bool transitionsCurrentBindlessSlots = false;
    bool transitionsMaterialContextSlots = false;
    bool transitionsCausticEmissionTargets = false;
    bool transitionsSurfelFrameConstants = false;
    bool transitionsShadowInstanceMaterials = false;
    bool transitionsShadowInstances = false;
    bool transitionsShadowMaterialTyped = false;
    bool transitionsSceneBvhNodes = false;
    bool transitionsSceneBvhInstances = false;
    bool transitionsSwBvhParent = false;
    bool transitionsSwBvhSortKeys = false;
    bool transitionsSwBvhSortPayload = false;
    bool transitionsSwBvhVisitCounter = false;
    bool transitionsSceneTlas = false;
    bool transitionsSceneTlasBacking = false;
    bool transitionsMeshBlasA = false;
    bool transitionsMeshBlasABacking = false;
    bool transitionsMeshBlasBBacking = false;
    bool transitionsMeshBlasAPosition = false;
    bool transitionsMeshBlasAIndex = false;
    for(usize index = 0u; index < compiledShadowPrepare->prologueBarrierCount; ++index){
        const Graphics::GpuCompiledBarrier& barrier = shadowPrepareBarriers[index];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ConstantBuffer
        ){
            transitionsCurrentBindlessSlots = transitionsCurrentBindlessSlots || barrier.resource == currentBindlessSlots;
            transitionsMaterialContextSlots = transitionsMaterialContextSlots || barrier.resource == materialContextSlots;
            transitionsSurfelFrameConstants = transitionsSurfelFrameConstants || barrier.resource == surfelFrameConstants;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ShaderResource
        ){
            transitionsCausticEmissionTargets = transitionsCausticEmissionTargets || barrier.resource == causticEmissionTargets;
            transitionsShadowInstanceMaterials = transitionsShadowInstanceMaterials || barrier.resource == shadowInstanceMaterials;
            transitionsShadowInstances = transitionsShadowInstances || barrier.resource == shadowInstances;
            transitionsShadowMaterialTyped = transitionsShadowMaterialTyped || barrier.resource == shadowMaterialTyped;
            transitionsSceneBvhNodes = transitionsSceneBvhNodes || barrier.resource == sceneBvhNodes;
            transitionsSceneBvhInstances = transitionsSceneBvhInstances || barrier.resource == sceneBvhInstances;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
        ){
            transitionsSwBvhParent = transitionsSwBvhParent || barrier.resource == swBvhParent;
            transitionsSwBvhSortKeys = transitionsSwBvhSortKeys || barrier.resource == swBvhSortKeys;
            transitionsSwBvhSortPayload = transitionsSwBvhSortPayload || barrier.resource == swBvhSortPayload;
            transitionsSwBvhVisitCounter = transitionsSwBvhVisitCounter || barrier.resource == swBvhVisitCounter;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::AccelStructTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::AccelStructWrite
        )
            transitionsSceneTlas = transitionsSceneTlas || barrier.resource == sceneTlas;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::AccelStructWrite
        ){
            transitionsSceneTlasBacking = transitionsSceneTlasBacking || barrier.resource == sceneTlasBacking;
            transitionsMeshBlasABacking = transitionsMeshBlasABacking || barrier.resource == meshBlasABacking;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::AccelStructTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::AccelStructWrite
        )
            transitionsMeshBlasA = transitionsMeshBlasA || barrier.resource == meshBlasA;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.after == Graphics::ResourceStates::AccelStructBuildInput
        ){
            transitionsMeshBlasAPosition = transitionsMeshBlasAPosition
                || (
                    barrier.resource == meshBlasAPosition
                    && barrier.before == Graphics::ResourceStates::Common
                )
            ;
            transitionsMeshBlasAIndex = transitionsMeshBlasAIndex
                || (
                    barrier.resource == meshBlasAIndex
                    && barrier.before == Graphics::ResourceStates::ShaderResource
                )
            ;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::AccelStructRead
        ){
            transitionsMeshBlasBBacking = transitionsMeshBlasBBacking || barrier.resource == meshBlasBBacking;
        }
    }
    EXPECT_TRUE(transitionsCurrentBindlessSlots);
    EXPECT_TRUE(transitionsMaterialContextSlots);
    EXPECT_TRUE(transitionsCausticEmissionTargets);
    EXPECT_TRUE(transitionsSurfelFrameConstants);
    EXPECT_TRUE(transitionsShadowInstanceMaterials);
    EXPECT_TRUE(transitionsShadowInstances);
    EXPECT_TRUE(transitionsShadowMaterialTyped);
    EXPECT_TRUE(transitionsSceneBvhNodes);
    EXPECT_TRUE(transitionsSceneBvhInstances);
    EXPECT_TRUE(transitionsSwBvhParent);
    EXPECT_TRUE(transitionsSwBvhSortKeys);
    EXPECT_TRUE(transitionsSwBvhSortPayload);
    EXPECT_TRUE(transitionsSwBvhVisitCounter);
    EXPECT_TRUE(transitionsSceneTlas);
    EXPECT_TRUE(transitionsSceneTlasBacking);
    EXPECT_TRUE(transitionsMeshBlasA);
    EXPECT_TRUE(transitionsMeshBlasABacking);
    EXPECT_TRUE(transitionsMeshBlasBBacking);
    EXPECT_TRUE(transitionsMeshBlasAPosition);
    EXPECT_TRUE(transitionsMeshBlasAIndex);
    const Graphics::GpuCompiledBarrier* const shadowPrepareHybridTailBarriers =
        compiledPlan.findTask(shadowPrepareHybridTail).prologueBarriers
    ;
    ASSERT_NE(shadowPrepareHybridTailBarriers, nullptr);
    const auto hasShadowPrepareHybridTailInputBarrier = [&](const Graphics::GpuGraphResourceId resource){
        for(usize barrierIndex = 0u;
            barrierIndex < compiledShadowPrepareHybridTail->prologueBarrierCount;
            ++barrierIndex
        ){
            const Graphics::GpuCompiledBarrier& barrier = shadowPrepareHybridTailBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::AccelStructBuildInput
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowPrepareHybridTailInputBarrier(meshBlasAPosition));
    EXPECT_TRUE(hasShadowPrepareHybridTailInputBarrier(meshBlasAIndex));
    const Graphics::GpuCompiledBarrier* const shadowPrepareTlasFinalizeBarriers =
        compiledPlan.findTask(shadowPrepareTlasFinalize).prologueBarriers
    ;
    ASSERT_NE(shadowPrepareTlasFinalizeBarriers, nullptr);
    const auto hasShadowPrepareTlasFinalizeBarrier = [&](
        const Graphics::GpuCompiledBarrierType::Enum type,
        const Graphics::GpuGraphResourceId resource
    ){
        for(usize barrierIndex = 0u;
            barrierIndex < compiledShadowPrepareTlasFinalize->prologueBarrierCount;
            ++barrierIndex
        ){
            const Graphics::GpuCompiledBarrier& barrier = shadowPrepareTlasFinalizeBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::AccelStructWrite
                && barrier.after == Graphics::ResourceStates::AccelStructRead
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowPrepareTlasFinalizeBarrier(
        Graphics::GpuCompiledBarrierType::AccelStructTransition,
        sceneTlas
    ));
    EXPECT_TRUE(hasShadowPrepareTlasFinalizeBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneTlasBacking
    ));
    EXPECT_TRUE(hasShadowPrepareTlasFinalizeBarrier(
        Graphics::GpuCompiledBarrierType::AccelStructTransition,
        meshBlasA
    ));
    EXPECT_TRUE(hasShadowPrepareTlasFinalizeBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        meshBlasABacking
    ));
    ASSERT_EQ(compiledPlan.packet(shadowVisibilityPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(shadowVisibilityPacket).dependencies[0u].producer, shadowPreparePacket);
}

TEST(GpuTaskGraph, MergesRayTraceMaterialContextUploadIntoShadowPreparePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/only_raytrace_material_context_slots"),
        "Ray-Trace Material Context Slots",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(materialContextSlots.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsUploadRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    uploadScheduling.forceSubmissionBoundary = false;
    uploadScheduling.allowPacketMerge = true;
    Graphics::GpuTaskSchedulingHint shadowPrepareScheduling;
    shadowPrepareScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowPrepareScheduling.forceSubmissionBoundary = false;
    shadowPrepareScheduling.allowPacketMerge = true;
    shadowPrepareScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse uploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/task_graph/raytrace_material_context_slots_upload"))
        .setMarkerLabel("Ray-Trace Material Context Slots Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(uploadScheduling)
        .setResourceUses(uploadUses, LengthOf(uploadUses))
    ;
    const Graphics::GpuTaskId upload = graph.addTask(uploadDesc);
    ASSERT_TRUE(upload.valid());

    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/raytrace_material_context_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(shadowPrepareScheduling)
        .setDependencies(&upload, 1u)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());

    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/raytrace_material_context_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibility = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibility.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId uploadPacket = compiledPlan.packetForTask(upload);
    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledPlan.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId shadowVisibilityPacket = compiledPlan.packetForTask(shadowVisibility);
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(shadowVisibilityPacket.valid());
    EXPECT_EQ(uploadPacket, shadowPreparePacket);
    EXPECT_NE(shadowPreparePacket, shadowVisibilityPacket);
    EXPECT_EQ(compiledPlan.packet(shadowPreparePacket).plan->taskCount, 2u);
    const Graphics::GpuSubmissionPacketRange shadowPrepareRange = compiledPlan.packetRange(
        shadowPreparePacket,
        shadowPreparePacket
    );
    ASSERT_TRUE(shadowPrepareRange.valid());
    EXPECT_EQ(shadowPrepareRange.packetCount, 1u);
    ASSERT_EQ(compiledPlan.packet(shadowVisibilityPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(shadowVisibilityPacket).dependencies[0u].producer, shadowPreparePacket);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

