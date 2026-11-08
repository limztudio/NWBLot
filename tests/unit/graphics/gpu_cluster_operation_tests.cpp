// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_GRAPHICS_BACKEND_VULKAN) && NWB_GRAPHICS_BACKEND_VULKAN

#include <core/graphics/vulkan/cluster_operation_input.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsBackend{
namespace __hidden_gpu_cluster_operation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuClusterOperation, NativeInputRebindsUnionPointersAfterCopy){
    for(const auto type : {
        RayTracingClusterOperationType::Move,
        RayTracingClusterOperationType::ClasBuild,
        RayTracingClusterOperationType::ClasBuildTemplates,
        RayTracingClusterOperationType::ClasInstantiateTemplates,
        RayTracingClusterOperationType::BlasBuild
    }){
        RayTracingClusterOperationParams params;
        params.type = type;
        params.maxArgCount = 7u;
        const auto built = VulkanDetail::ClusterOperationInput::build(params, NWB_TEXT("test cluster input"));
        ASSERT_TRUE(built);
        auto copied = *built;
        const auto native = copied.nativeInfo();
        EXPECT_EQ(native.maxAccelerationStructureCount, 7u);
        if(type == RayTracingClusterOperationType::Move)
            EXPECT_EQ(native.opInput.pMoveObjects, &copied.move);
        else if(type == RayTracingClusterOperationType::BlasBuild)
            EXPECT_EQ(native.opInput.pClustersBottomLevel, &copied.blas);
        else
            EXPECT_EQ(native.opInput.pTriangleClusters, &copied.cluster);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif

