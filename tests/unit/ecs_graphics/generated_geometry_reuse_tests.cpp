// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/generated_geometry_reuse.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_generated_geometry_reuse_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct GeneratedGeometryReuseTestsTag>;

class ReuseContext final{
public:
    ReuseContext(){
        source = makeBuffer("tests/geometry_reuse/source");
        alternate = makeBuffer("tests/geometry_reuse/alternate");
        instances.resize(3u);
        for(u32 index = 0u; index < 2u; ++index){
            MaterialPassDrawItem draw;
            draw.meshKey = Name(index == 0u ? "tests/geometry_reuse/first" : "tests/geometry_reuse/second");
            draw.pipelineKey.material = Name("tests/geometry_reuse/material");
            draw.pipelineKey.pass = MaterialPipelinePass::AvboitOccupancy;
            draw.pipelineResources.sharedGeometryComputeProgram = true;
            draw.instanceIndex = index;
            draw.materialConstantByteOffset = index * 16u;
            draw.shadingModelId = 2u;
            draw.meshletConeCullScaleSafe = true;
            auto& mesh = draw.meshResources;
            mesh.sourceBuffers = RuntimeMeshBuffers{
                .positionBuffer = source,
                .normalBuffer = source,
                .tangentBuffer = source,
                .uv0Buffer = source,
                .colorBuffer = source,
                .meshletDescBuffer = source,
                .meshletBoundsBuffer = source,
                .meshletPositionRefDeltaBuffer = source,
                .meshletAttributeRefDeltaBuffer = source,
                .meshletLocalVertexRefBuffer = source,
                .meshletPrimitiveIndexBuffer = source,
            };
            for(u32 slot = 0u; slot < LengthOf(mesh.geometryHeapHandles); ++slot){
                mesh.geometryHeapHandles[slot] = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, slot);
                instances[index].geometryHeapSlots[slot] = slot;
            }
            mesh.emulationVertexBuffer = makeBuffer(index == 0u ? "tests/geometry_reuse/output_a" : "tests/geometry_reuse/output_b");
            mesh.emulationVertexHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 20u + index);
            mesh.meshletCount = 1u;
            mesh.meshletPrimitiveIndexCount = 3u;
            mesh.runtimeMesh = true;
            mesh.dynamicMeshletBoundsFresh = true;
            mesh.dynamicMeshletConesFresh = true;
            draws.regular.computeDrawItems.push_back(draw);
            instances[index].translation.x = static_cast<f32>(index);
        }
        bindings.instanceBuffer = makeBuffer("tests/geometry_reuse/instances");
        bindings.materialTypedBuffer = makeBuffer("tests/geometry_reuse/material_bytes");
        bindings.meshView.buffer = makeBuffer("tests/geometry_reuse/view");
        bindings.meshView.heapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::UniformBuffer, 1u);
        bindings.instanceHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 30u);
        bindings.materialTypedHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 31u);
        bindings.instanceBufferCapacity = 3u;
        bindings.materialTypedBufferCapacity = 256u;
    }


public:
    [[nodiscard]] Core::BufferHandle makeBuffer(const char* identity){
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(
            testArena.arena, context, allocator, Core::BufferDesc{}.setByteSize(512u).setDebugName(Name(identity))
        );
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    void setPass(const MaterialPipelinePass::Enum pass){
        for(auto& draw : draws.regular.computeDrawItems)
            draw.pipelineKey.pass = pass;
    }

    [[nodiscard]] bool capture(const MaterialPipelinePass::Enum pass = MaterialPipelinePass::AvboitOccupancy){
        setPass(pass);
        return plan.capture(draws, instances, bindings, view, pass);
    }

    [[nodiscard]] bool captureAndPublish(){
        return capture() && plan.publishProducer(producer);
    }

    [[nodiscard]] bool matches(const MaterialPipelinePass::Enum pass = MaterialPipelinePass::AvboitExtinction){
        setPass(pass);
        return plan.matches(draws, instances, bindings, view, pass);
    }


public:
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::Alloc::ScratchArena scratch{ Name("tests/geometry_reuse/scratch") };
    Core::BufferHandle source;
    Core::BufferHandle alternate;
    MaterialPassDrawItemPartitions draws{ scratch };
    InstanceGpuDataVector instances{ scratch };
    ECSRenderDetail::MeshFrameBindingSnapshot bindings;
    ECSRenderDetail::MeshViewGpuData view;
    AvboitGeneratedGeometryReuse plan{ scratch };
    Core::GpuTaskId producer{ .generation = 4u, .index = 7u };
};

TEST(AvboitGeneratedGeometryReuse, PublishedGroupServesAllEligiblePassesWithoutChangingProducerOrConsumerMetadata){
    ReuseContext fixture;
    ASSERT_TRUE(fixture.captureAndPublish());
    for(const auto pass : { MaterialPipelinePass::AvboitRefractionCapture, MaterialPipelinePass::AvboitOccupancy,
        MaterialPipelinePass::AvboitExtinction, MaterialPipelinePass::AvboitAccumulate }){
        EXPECT_TRUE(fixture.matches(pass));
        EXPECT_EQ(fixture.plan.producerTask(), fixture.producer);
        EXPECT_EQ(fixture.draws.regular.computeDrawItems[0u].pipelineKey.pass, pass);
    }
}

TEST(AvboitGeneratedGeometryReuse, UnifiedOutputLayoutAndRepresentationMustMatchAcrossPasses){
    ReuseContext fixture;
    auto& draw = fixture.draws.regular.computeDrawItems[0u];
    draw.meshResources.emulationIndexByteOffset = 256u;
    draw.pipelineResources.indexedGeometryOutput = true;
    ASSERT_TRUE(fixture.captureAndPublish());
    EXPECT_TRUE(fixture.matches());

    draw.meshResources.emulationIndexByteOffset = 512u;
    EXPECT_FALSE(fixture.matches());
    EXPECT_FALSE(fixture.plan.producerTask().valid());
    ASSERT_TRUE(fixture.captureAndPublish());
    EXPECT_TRUE(fixture.matches());

    draw.pipelineResources.indexedGeometryOutput = false;
    EXPECT_FALSE(fixture.matches());
    EXPECT_FALSE(fixture.plan.producerTask().valid());
    ASSERT_TRUE(fixture.captureAndPublish());
    EXPECT_TRUE(fixture.matches());
    draw.pipelineResources.indexedGeometryOutput = true;
    EXPECT_FALSE(fixture.matches());
}

TEST(AvboitGeneratedGeometryReuse, PersistentIndexedDrawsDoNotAlterGeneratedComputeContents){
    ReuseContext fixture;
    MaterialPassDrawItem indexed = fixture.draws.regular.computeDrawItems.front();
    indexed.meshKey = Name("tests/geometry_reuse/indexed");
    indexed.meshResources.emulationVertexBuffer.reset();
    indexed.meshResources.objectGeometryCache.buffer = fixture.source;
    indexed.meshResources.objectGeometryCache.sourceRevision = 8u;
    fixture.draws.regular.indexedDrawItems.push_back(indexed);
    ASSERT_TRUE(fixture.captureAndPublish());
    EXPECT_TRUE(fixture.matches());
    auto& cache = fixture.draws.regular.indexedDrawItems.front().meshResources.objectGeometryCache;
    cache.sourceRevision = 9u;
    cache.buffer = fixture.alternate;
    EXPECT_TRUE(fixture.matches());
    fixture.draws.regular.indexedDrawItems.clear();
    EXPECT_TRUE(fixture.matches());
}

TEST(AvboitGeneratedGeometryReuse, StagedOrInvalidProducerCannotPublishReusableContents){
    ReuseContext fixture;
    ASSERT_TRUE(fixture.capture());
    EXPECT_FALSE(fixture.plan.producerTask().valid());
    EXPECT_FALSE(fixture.matches());
    EXPECT_FALSE(fixture.plan.publishProducer(fixture.producer));
    ASSERT_TRUE(fixture.capture());
    EXPECT_FALSE(fixture.plan.publishProducer({}));
    EXPECT_FALSE(fixture.matches());
}

TEST(AvboitGeneratedGeometryReuse, SelectedInstanceBytesMustMatchButUnreferencedInstancesDoNotMatter){
    ReuseContext fixture;
    ASSERT_TRUE(fixture.captureAndPublish());
    fixture.instances[2u].translation.x = 17.f;
    EXPECT_TRUE(fixture.matches());
    fixture.instances[1u].translation.x = 12.f;
    EXPECT_FALSE(fixture.matches());
    EXPECT_FALSE(fixture.plan.producerTask().valid());
    fixture.instances[1u].translation.x = 1.f;
    EXPECT_FALSE(fixture.matches());
    ASSERT_TRUE(fixture.captureAndPublish());
    ++fixture.instances[0u].translation.w;
    EXPECT_FALSE(fixture.matches());
}

TEST(AvboitGeneratedGeometryReuse, SharedOutputPointerOrDescriptorSlotRejectsEntireGroup){
    for(const bool shareBuffer : { false, true }){
        ReuseContext fixture;
        ASSERT_TRUE(fixture.captureAndPublish());
        auto& first = fixture.draws.regular.computeDrawItems[0u].meshResources;
        auto& second = fixture.draws.regular.computeDrawItems[1u].meshResources;
        if(shareBuffer)
            second.emulationVertexBuffer = first.emulationVertexBuffer;
        else
            second.emulationVertexHeapHandle = first.emulationVertexHeapHandle;
        EXPECT_FALSE(fixture.matches());
        EXPECT_FALSE(fixture.capture());
        EXPECT_FALSE(fixture.plan.producerTask().valid());
    }
}

TEST(AvboitGeneratedGeometryReuse, MixedOrIncompatiblePhaseForgetsPriorGeneratedContents){
    for(u32 kind = 0u; kind < 5u; ++kind){
        ReuseContext fixture;
        ASSERT_TRUE(fixture.captureAndPublish());
        const auto original = fixture.draws.regular.computeDrawItems[0u];
        if(kind == 0u)
            fixture.draws.regular.meshDrawItems.push_back(original);
        else if(kind == 1u)
            fixture.draws.csg.computeDrawItems.push_back(original);
        else if(kind == 2u)
            fixture.draws.csgReceiverSurface.computeDrawItems.push_back(original);
        else if(kind == 3u)
            fixture.draws.regular.computeDrawItems[0u].pipelineResources.sharedGeometryComputeProgram = false;
        else
            fixture.draws.regular.computeDrawItems[0u].pipelineKey.csgMode = MaterialPipelineCsgMode::ClipOnly;
        EXPECT_FALSE(fixture.matches());
        fixture.draws.regular.meshDrawItems.clear();
        fixture.draws.csg.computeDrawItems.clear();
        fixture.draws.csgReceiverSurface.computeDrawItems.clear();
        fixture.draws.regular.computeDrawItems[0u] = original;
        EXPECT_FALSE(fixture.matches());
    }
}

TEST(AvboitGeneratedGeometryReuse, EverySourceBufferAndFullDescriptorIdentityParticipates){
    constexpr Core::BufferHandle RuntimeMeshBuffers::* s_SourceMembers[]{
        &RuntimeMeshBuffers::positionBuffer, &RuntimeMeshBuffers::normalBuffer, &RuntimeMeshBuffers::tangentBuffer,
        &RuntimeMeshBuffers::uv0Buffer, &RuntimeMeshBuffers::colorBuffer, &RuntimeMeshBuffers::meshletDescBuffer,
        &RuntimeMeshBuffers::meshletBoundsBuffer, &RuntimeMeshBuffers::meshletPositionRefDeltaBuffer,
        &RuntimeMeshBuffers::meshletAttributeRefDeltaBuffer, &RuntimeMeshBuffers::meshletLocalVertexRefBuffer,
        &RuntimeMeshBuffers::meshletPrimitiveIndexBuffer,
    };
    for(const auto member : s_SourceMembers){
        ReuseContext fixture;
        ASSERT_TRUE(fixture.captureAndPublish());
        fixture.draws.regular.computeDrawItems[0u].meshResources.sourceBuffers.*member = fixture.alternate;
        EXPECT_FALSE(fixture.matches());
    }
    ReuseContext fixture;
    ASSERT_TRUE(fixture.captureAndPublish());
    fixture.draws.regular.computeDrawItems[0u].meshResources.geometryHeapHandles[0u] =
        Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 50u);
    EXPECT_FALSE(fixture.matches());
    ASSERT_TRUE(fixture.captureAndPublish());
    fixture.draws.regular.computeDrawItems[0u].meshResources.emulationVertexHeapHandle =
        Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::UniformBuffer, 20u);
    EXPECT_FALSE(fixture.matches());
}

TEST(AvboitGeneratedGeometryReuse, ViewContentsAndFrameBindingReplacementInvalidateReuse){
    ReuseContext fixture;
    ASSERT_TRUE(fixture.captureAndPublish());
    fixture.view.frustumPlanes[0u].w += 1.f;
    EXPECT_FALSE(fixture.matches());
    ASSERT_TRUE(fixture.captureAndPublish());
    fixture.bindings.meshView.buffer = fixture.alternate;
    EXPECT_FALSE(fixture.matches());
    ASSERT_TRUE(fixture.captureAndPublish());
    fixture.bindings.instanceHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 42u);
    EXPECT_FALSE(fixture.matches());
}

TEST(AvboitGeneratedGeometryReuse, DrawOrderCountsAndOriginatingCullInputsMustMatch){
    ReuseContext fixture;
    ASSERT_TRUE(fixture.captureAndPublish());
    Swap(fixture.draws.regular.computeDrawItems[0u], fixture.draws.regular.computeDrawItems[1u]);
    EXPECT_FALSE(fixture.matches());
    ASSERT_TRUE(fixture.captureAndPublish());
    fixture.draws.regular.computeDrawItems[0u].meshResources.dynamicMeshletBoundsFresh = false;
    EXPECT_FALSE(fixture.matches());
    ASSERT_TRUE(fixture.captureAndPublish());
    ++fixture.draws.regular.computeDrawItems[0u].meshResources.meshletPrimitiveIndexCount;
    EXPECT_FALSE(fixture.matches());
    ASSERT_TRUE(fixture.captureAndPublish());
    fixture.draws.regular.computeDrawItems.pop_back();
    EXPECT_FALSE(fixture.matches());
}

TEST(AvboitGeneratedGeometryReuse, AllOriginatingCullFlagsAndConservativeMaterialKeysParticipate){
    for(u32 change = 0u; change < 8u; ++change){
        ReuseContext fixture;
        ASSERT_TRUE(fixture.captureAndPublish());
        auto& draw = fixture.draws.regular.computeDrawItems[0u];
        switch(change){
        case 0u: draw.meshResources.runtimeMesh = false; break;
        case 1u: draw.meshResources.dynamicMeshletConesFresh = false; break;
        case 2u: draw.meshletConeCullScaleSafe = false; break;
        case 3u: draw.pipelineKey.twoSided = true; break;
        case 4u: ++draw.materialConstantByteOffset; break;
        case 5u: ++draw.shadingModelId; break;
        case 6u: draw.pipelineKey.material = Name("tests/geometry_reuse/changed_material"); break;
        case 7u: ++draw.meshResources.meshletCount; break;
        }
        EXPECT_FALSE(fixture.matches());
    }
}

TEST(AvboitGeneratedGeometryReuse, EmptyOutOfRangeAndNonAvboitGroupsClearHistory){
    ReuseContext fixture;
    ASSERT_TRUE(fixture.captureAndPublish());
    EXPECT_FALSE(fixture.matches(MaterialPipelinePass::Opaque));
    ASSERT_TRUE(fixture.captureAndPublish());
    fixture.draws.regular.computeDrawItems[0u].instanceIndex = 3u;
    EXPECT_FALSE(fixture.matches());
    EXPECT_FALSE(fixture.capture());
    fixture.draws.regular.computeDrawItems.clear();
    EXPECT_FALSE(fixture.capture());
    EXPECT_FALSE(fixture.plan.producerTask().valid());
}

TEST(AvboitGeneratedGeometryReuse, SnapshotOwnsResourcesAndResetReleasesThem){
    ReuseContext fixture;
    const u32 references = fixture.source->getReferenceCount();
    ASSERT_TRUE(fixture.captureAndPublish());
    EXPECT_EQ(fixture.source->getReferenceCount(), references + 22u);
    fixture.draws.regular.computeDrawItems.clear();
    EXPECT_EQ(fixture.source->getReferenceCount(), references);
    fixture.plan.reset();
    EXPECT_EQ(fixture.source->getReferenceCount(), references - 22u);
    EXPECT_FALSE(fixture.plan.producerTask().valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

