// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "shaders_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A sampler/resource mix is not segment-coherent. The renderer never falls back to ordinary descriptor sets, so
// callers must split this shape into separate resource and sampler layouts.
TEST_F(DescriptorBufferRoundTripTest, MixedDescriptorBufferLayoutIsRejected){
    auto& device = DescriptorBufferRoundTripTest::device();

    BindlessLayoutDesc layoutDesc;
    layoutDesc
        .setLayoutType(BindlessLayoutType::Immutable)
        .setMaxCapacity(1u)
        .setDescriptorSetIndex(NWB_BINDLESS_HEAP_RESOURCE_SET)
        .setVisibility(ShaderType::Compute)
        .addRegisterSpace(BindingLayoutItem::Texture_SRV(0u, 1u))
        .addRegisterSpace(BindingLayoutItem::Sampler(1u, 1u))
    ;
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(device.createBindlessLayout(layoutDesc));
    }, "");
#else
    auto layout = device.createBindlessLayout(layoutDesc);
    EXPECT_FALSE(layout);
#endif
}


// Explicit resource layouts must cover every set from zero through the highest set. Vulkan pipeline layouts cannot
// express a hole without manufacturing an unrelated placeholder layout, so sparse ABI composition fails closed.
TEST_F(DescriptorBufferRoundTripTest, SparseExplicitDescriptorSetLayoutsAreRejected){
    auto& device = DescriptorBufferRoundTripTest::device();

    BindlessLayoutDesc resourceDesc;
    resourceDesc
        .setLayoutType(BindlessLayoutType::Immutable)
        .setMaxCapacity(1u)
        .setDescriptorSetIndex(0u)
        .setVisibility(ShaderType::Compute)
        .addRegisterSpace(BindingLayoutItem::StructuredBuffer_UAV(0u, 1u))
    ;
    BindlessLayoutDesc samplerDesc;
    samplerDesc
        .setLayoutType(BindlessLayoutType::MutableSampler)
        .setMaxCapacity(1u)
        .setDescriptorSetIndex(2u)
        .setVisibility(ShaderType::Compute)
        .addRegisterSpace(BindingLayoutItem::Sampler(0u, 1u))
    ;
    const BindingLayoutHandle resourceLayout = device.createBindlessLayout(resourceDesc);
    const BindingLayoutHandle samplerLayout = device.createBindlessLayout(samplerDesc);
    ASSERT_TRUE(resourceLayout);
    ASSERT_TRUE(samplerLayout);

    ShaderDesc shaderDesc(DescriptorBufferRoundTripTest::arena());
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name{"tests/descriptor_buffer/sparse_explicit_sets"})
    ;
    const ShaderHandle shader = device.createShader(
        shaderDesc,
        s_DescriptorHeapRetirementComputeSpirv,
        sizeof(s_DescriptorHeapRetirementComputeSpirv)
    );
    ASSERT_TRUE(shader);

    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(resourceLayout)
        .addBindingLayout(samplerLayout)
    ;
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(device.createComputePipeline(pipelineDesc));
    }, "");
#else
    EXPECT_FALSE(device.createComputePipeline(pipelineDesc));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

