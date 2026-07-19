// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::runBvhSortSelfTest(){
    if(rayTracingState().m_bvhSortSelfTestDone)
        return;
    rayTracingState().m_bvhSortSelfTestDone = true;

    // A wrong sort silently corrupts every BVH built on top of it, so verify the kernel directly in debug
    // builds: sort a reversed sequence with an identity payload and read it back. The ascending result must
    // be exactly 0..elementCount-1, with the sentinel-padded tail still non-decreasing.
    constexpr u32 elementCount = 1000u;
    constexpr u32 paddedCount = 1024u;
    static_assert(paddedCount >= static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE), "self-test padded count must cover one dispatch group");

    if(!ensureBvhSortPipeline())
        return;
    if(!ensureBvhSortBuffers(paddedCount))
        return;

    auto* device = graphics().getDevice();

    u32 inputKeys[paddedCount];
    u32 inputPayload[paddedCount];
    for(u32 i = 0u; i < paddedCount; ++i){
        inputKeys[i] = i < elementCount ? (elementCount - 1u - i) : BvhNodeIndex::Invalid;
        inputPayload[i] = i;
    }

    Core::BufferDesc readbackBufferDesc;
    readbackBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * paddedCount))
        .setCpuAccess(Core::CpuAccessMode::Read)
        .setDebugName(Name("bvh_sort_selftest_readback"))
        .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
    ;
    Core::BufferHandle readbackBuffer = graphics().createBuffer(readbackBufferDesc);
    if(!readbackBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort self-test readback buffer"));
        return;
    }

    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort self-test command list"));
        return;
    }

    commandList->open();
    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->setBufferState(rayTracingState().m_bvhSortPayloadBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->commitBarriers();
    commandList->writeBuffer(rayTracingState().m_bvhSortKeysBuffer.get(), inputKeys, sizeof(inputKeys));
    commandList->writeBuffer(rayTracingState().m_bvhSortPayloadBuffer.get(), inputPayload, sizeof(inputPayload));
    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList->setBufferState(rayTracingState().m_bvhSortPayloadBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();

    if(!bvhBitonicSort(*commandList, elementCount, paddedCount)){
        commandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH sort self-test dispatch setup failed"));
        return;
    }

    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::CopySource);
    commandList->commitBarriers();
    commandList->copyBuffer(readbackBuffer.get(), 0u, rayTracingState().m_bvhSortKeysBuffer.get(), 0u, static_cast<u64>(sizeof(u32) * paddedCount));
    commandList->close();

    Core::CommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH sort self-test wait-for-idle failed"));
        return;
    }

    const u32* sortedKeys = static_cast<const u32*>(device->mapBuffer(readbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!sortedKeys){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH sort self-test readback buffer"));
        return;
    }

    bool sorted = true;
    for(u32 i = 0u; i < elementCount; ++i){
        if(sortedKeys[i] != i){
            sorted = false;
            break;
        }
    }
    for(u32 i = 0u; sorted && (i + 1u) < paddedCount; ++i){
        if(sortedKeys[i] > sortedKeys[i + 1u]){
            sorted = false;
            break;
        }
    }
    device->unmapBuffer(readbackBuffer.get());

    if(sorted)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH bitonic sort self-test PASSED ({} elements)"), elementCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH bitonic sort self-test FAILED"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::runBvhBuildSelfTest(){
    if(rayTracingState().m_bvhBuildSelfTestDone)
        return;
    rayTracingState().m_bvhBuildSelfTestDone = true;

    // Build an LBVH for a known triangle set and validate it on the CPU. A wrong topology can still bound
    // everything correctly, so this checks leaf coverage and child-box nesting, not just the root box.
    constexpr u32 triangleCount = 16u;
    constexpr u32 vertexCount = triangleCount * 3u;
    constexpr u32 floatCount = vertexCount * 3u;
    constexpr u32 nodeCount = triangleCount * 2u - 1u;
    constexpr u32 internalCount = triangleCount - 1u;

    f32 positionData[floatCount];
    u32 indexData[vertexCount];
    for(u32 triangle = 0u; triangle < triangleCount; ++triangle){
        const f32 baseX = static_cast<f32>(triangle);
        const u32 floatBase = triangle * 9u;
        positionData[floatBase + 0u] = baseX;         positionData[floatBase + 1u] = 0.0f;  positionData[floatBase + 2u] = 0.0f;
        positionData[floatBase + 3u] = baseX + 0.6f;  positionData[floatBase + 4u] = 0.8f;  positionData[floatBase + 5u] = 0.1f;
        positionData[floatBase + 6u] = baseX + 0.2f;  positionData[floatBase + 7u] = 0.1f;  positionData[floatBase + 8u] = 0.9f;
    }
    for(u32 index = 0u; index < vertexCount; ++index)
        indexData[index] = index;

    SIMDVector boundsMinVector = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVector boundsMaxVector = VectorReplicate(-s_RayTracingFiniteInfinity);
    for(u32 vertex = 0u; vertex < vertexCount; ++vertex){
        const SIMDVector position = VectorSet(positionData[vertex * 3u + 0u], positionData[vertex * 3u + 1u], positionData[vertex * 3u + 2u], 0.0f);
        boundsMinVector = VectorMin(boundsMinVector, position);
        boundsMaxVector = VectorMax(boundsMaxVector, position);
    }
    auto* device = graphics().getDevice();

    // Per-mesh BVH storage lives with the caller (here, the self-test); the build/refit helpers create
    // and reuse these on first use, mirroring MeshResources on the real path.
    Core::BufferHandle testNodeBuffer;
    Core::BufferHandle testParentBuffer;
    Core::BindingSetHandle testBindingSet;

    Core::BufferDesc positionBufferDesc;
    positionBufferDesc
        .setByteSize(sizeof(positionData))
        .setCanHaveRawViews(true)
        .setDebugName(Name("bvh_build_selftest_positions"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle positionBuffer = graphics().createBuffer(positionBufferDesc);

    Core::BufferDesc indexBufferDesc;
    indexBufferDesc
        .setByteSize(sizeof(indexData))
        .setCanHaveRawViews(true)
        .setDebugName(Name("bvh_build_selftest_indices"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle indexBuffer = graphics().createBuffer(indexBufferDesc);
    if(!positionBuffer || !indexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build self-test geometry buffers"));
        return;
    }

    Core::BufferDesc readbackBufferDesc;
    readbackBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount))
        .setCpuAccess(Core::CpuAccessMode::Read)
        .setDebugName(Name("bvh_build_selftest_readback"))
        .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
    ;
    Core::BufferHandle readbackBuffer = graphics().createBuffer(readbackBufferDesc);
    if(!readbackBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build self-test readback buffer"));
        return;
    }

    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build self-test command list"));
        return;
    }

    commandList->open();
    commandList->setBufferState(positionBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->setBufferState(indexBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->commitBarriers();
    commandList->writeBuffer(positionBuffer.get(), positionData, sizeof(positionData));
    commandList->writeBuffer(indexBuffer.get(), indexData, sizeof(indexData));
    commandList->setBufferState(positionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList->setBufferState(indexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    if(!buildMeshSwBvh(*commandList, positionBuffer.get(), indexBuffer.get(), triangleCount, boundsMinVector, boundsMaxVector, testNodeBuffer, testParentBuffer, testBindingSet)){
        commandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH build self-test build failed"));
        return;
    }

    commandList->setBufferState(testNodeBuffer.get(), Core::ResourceStates::CopySource);
    commandList->commitBarriers();
    commandList->copyBuffer(readbackBuffer.get(), 0u, testNodeBuffer.get(), 0u, static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount));
    commandList->close();

    Core::CommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH build self-test wait-for-idle failed"));
        return;
    }

    const NwbBvhNodeGpu* nodes = static_cast<const NwbBvhNodeGpu*>(device->mapBuffer(readbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!nodes){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH build self-test readback buffer"));
        return;
    }

    bool valid = true;

    // (1) Leaf coverage: every leaf node carries the flag + a unique primitive, and all primitives appear.
    bool primitiveSeen[triangleCount] = {};
    for(u32 leaf = 0u; valid && leaf < triangleCount; ++leaf){
        const NwbBvhNodeGpu& node = nodes[internalCount + leaf];
        if((node.aabbMinLeftChild.w & BvhNodeIndex::LeafFlag) == 0u){
            valid = false;
            break;
        }
        const u32 primitive = node.aabbMinLeftChild.w & ~BvhNodeIndex::LeafFlag;
        if(primitive >= triangleCount || primitiveSeen[primitive]){
            valid = false;
            break;
        }
        primitiveSeen[primitive] = true;
    }
    for(u32 primitive = 0u; valid && primitive < triangleCount; ++primitive){
        if(!primitiveSeen[primitive])
            valid = false;
    }

    // (2) Root box matches the input bounds.
    const SIMDVector epsilonVector = VectorReplicate(1e-3f);
    if(valid){
        const SIMDVector rootMin = LoadFloatInt(nodes[0].aabbMinLeftChild);
        const SIMDVector rootMax = LoadFloatInt(nodes[0].aabbMaxRightChild);
        if(
            !Vector3LessOrEqual(VectorAbs(VectorSubtract(rootMin, boundsMinVector)), epsilonVector)
            || !Vector3LessOrEqual(VectorAbs(VectorSubtract(rootMax, boundsMaxVector)), epsilonVector)
        )
            valid = false;
    }

    // (3) Every internal node references valid children and its box contains both child boxes.
    for(u32 internal = 0u; valid && internal < internalCount; ++internal){
        const NwbBvhNodeGpu& node = nodes[internal];
        if(node.aabbMinLeftChild.w >= nodeCount || node.aabbMaxRightChild.w >= nodeCount){
            valid = false;
            break;
        }
        const NwbBvhNodeGpu& left = nodes[node.aabbMinLeftChild.w];
        const NwbBvhNodeGpu& right = nodes[node.aabbMaxRightChild.w];
        const SIMDVector childMin = VectorMin(LoadFloatInt(left.aabbMinLeftChild), LoadFloatInt(right.aabbMinLeftChild));
        const SIMDVector childMax = VectorMax(LoadFloatInt(left.aabbMaxRightChild), LoadFloatInt(right.aabbMaxRightChild));
        if(
            !Vector3LessOrEqual(LoadFloatInt(node.aabbMinLeftChild), VectorAdd(childMin, epsilonVector))
            || !Vector3GreaterOrEqual(LoadFloatInt(node.aabbMaxRightChild), VectorSubtract(childMax, epsilonVector))
        )
            valid = false;
    }

    device->unmapBuffer(readbackBuffer.get());

    if(valid)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH build self-test PASSED ({} triangles, {} nodes)"), triangleCount, nodeCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH build self-test FAILED"));

    // Refit phase: translate every vertex and refit the EXISTING topology. A refit reuses the build-pose
    // tree and only recomputes boxes from the current positions, so the root box must track the translation.
    for(u32 vertex = 0u; vertex < vertexCount; ++vertex)
        positionData[vertex * 3u + 0u] += 5.0f;
    const SIMDVector refitOffset = VectorSet(5.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector refitMin = VectorAdd(boundsMinVector, refitOffset);
    const SIMDVector refitMax = VectorAdd(boundsMaxVector, refitOffset);

    Core::BufferHandle refitReadbackBuffer = graphics().createBuffer(readbackBufferDesc);
    Core::CommandListHandle refitCommandList = device->createCommandList();
    if(!refitReadbackBuffer || !refitCommandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH refit self-test resources"));
        return;
    }

    refitCommandList->open();
    refitCommandList->setBufferState(positionBuffer.get(), Core::ResourceStates::CopyDest);
    refitCommandList->commitBarriers();
    refitCommandList->writeBuffer(positionBuffer.get(), positionData, sizeof(positionData));
    refitCommandList->setBufferState(positionBuffer.get(), Core::ResourceStates::ShaderResource);
    refitCommandList->commitBarriers();

    if(!refitMeshSwBvh(*refitCommandList, positionBuffer.get(), indexBuffer.get(), triangleCount, testNodeBuffer, testParentBuffer, testBindingSet)){
        refitCommandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH refit self-test refit failed"));
        return;
    }

    refitCommandList->setBufferState(testNodeBuffer.get(), Core::ResourceStates::CopySource);
    refitCommandList->commitBarriers();
    refitCommandList->copyBuffer(refitReadbackBuffer.get(), 0u, testNodeBuffer.get(), 0u, static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount));
    refitCommandList->close();

    Core::CommandList* refitCommandLists[] = { refitCommandList.get() };
    device->executeCommandLists(refitCommandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH refit self-test wait-for-idle failed"));
        return;
    }

    const NwbBvhNodeGpu* refitNodes = static_cast<const NwbBvhNodeGpu*>(device->mapBuffer(refitReadbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!refitNodes){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH refit self-test readback buffer"));
        return;
    }
    const bool refitValid =
        Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloatInt(refitNodes[0].aabbMinLeftChild), refitMin)), epsilonVector)
        && Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloatInt(refitNodes[0].aabbMaxRightChild), refitMax)), epsilonVector)
    ;
    device->unmapBuffer(refitReadbackBuffer.get());

    if(refitValid)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH refit self-test PASSED ({} triangles)"), triangleCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH refit self-test FAILED"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::runSceneBvhSelfTest(){
    if(rayTracingState().m_sceneBvhSelfTestDone)
        return;
    rayTracingState().m_sceneBvhSelfTestDone = true;

    // The scene/instance BVH is CPU-built, so validate the builder directly: build a tree over known instance
    // AABBs and check leaf coverage, the root box, and child-box nesting (a wrong topology can still bound
    // everything, so the structural checks matter, not just the root box).
    constexpr u32 instanceCount = 12u;
    constexpr u32 nodeCount = instanceCount * 2u - 1u;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RayTracingBuildArena, 16u * 1024u);

    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMin{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMax{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceCentroid{ scratchArena };
    instanceAabbMin.reserve(instanceCount);
    instanceAabbMax.reserve(instanceCount);
    instanceCentroid.reserve(instanceCount);

    SIMDVector boundsMin = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVector boundsMax = VectorReplicate(-s_RayTracingFiniteInfinity);
    for(u32 i = 0u; i < instanceCount; ++i){
        const f32 base = static_cast<f32>(i) * 3.0f;
        const SIMDVector boxMin = VectorSet(base, base * 0.25f, -base * 0.5f, 0.0f);
        const SIMDVector boxMax = VectorAdd(boxMin, VectorSet(1.5f, 2.0f, 1.0f, 0.0f));

        Float4 storedBoxMin;
        Float4 storedBoxMax;
        Float4 storedCentroid;
        StoreFloat(boxMin, &storedBoxMin);
        StoreFloat(boxMax, &storedBoxMax);
        StoreFloat(VectorScale(VectorAdd(boxMin, boxMax), 0.5f), &storedCentroid);

        instanceAabbMin.push_back(storedBoxMin);
        instanceAabbMax.push_back(storedBoxMax);
        instanceCentroid.push_back(storedCentroid);

        boundsMin = VectorMin(boundsMin, boxMin);
        boundsMax = VectorMax(boundsMax, boxMax);
    }

    Vector<u32, Core::Alloc::ScratchArena> indices{ scratchArena };
    indices.reserve(instanceCount);
    for(u32 i = 0u; i < instanceCount; ++i)
        indices.push_back(i);

    Vector<SceneBvhNodeBuildData, Core::Alloc::ScratchArena> nodes{ scratchArena };
    nodes.reserve(nodeCount);
    __hidden_raytracing_system::BuildSceneBvhNode(indices.data(), 0u, instanceCount, instanceAabbMin.data(), instanceAabbMax.data(), instanceCentroid.data(), nodes);

    bool valid = (nodes.size() == nodeCount);

    // (1) Leaf coverage: every instance appears in exactly one leaf, and every leaf is a valid instance.
    bool instanceSeen[instanceCount] = {};
    u32 leafCount = 0u;
    for(u32 i = 0u; valid && i < nodes.size(); ++i){
        const u32 leftChild = nodes[i].leftChild;
        if((leftChild & BvhNodeIndex::LeafFlag) == 0u)
            continue;
        ++leafCount;
        const u32 instance = leftChild & ~BvhNodeIndex::LeafFlag;
        if(instance >= instanceCount || instanceSeen[instance]){
            valid = false;
            break;
        }
        instanceSeen[instance] = true;
    }
    if(valid && leafCount != instanceCount)
        valid = false;
    for(u32 i = 0u; valid && i < instanceCount; ++i){
        if(!instanceSeen[i])
            valid = false;
    }

    // (2) Root box == union of all instance AABBs.
    const SIMDVector epsilonVector = VectorReplicate(1e-3f);
    if(valid){
        if(
            !Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloat(nodes[0].aabbMin), boundsMin)), epsilonVector)
            || !Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloat(nodes[0].aabbMax), boundsMax)), epsilonVector)
        )
            valid = false;
    }

    // (3) Every internal node references valid children and its box contains both child boxes.
    for(u32 i = 0u; valid && i < nodes.size(); ++i){
        const u32 leftChild = nodes[i].leftChild;
        if((leftChild & BvhNodeIndex::LeafFlag) != 0u)
            continue;
        const u32 rightChild = nodes[i].rightChild;
        if(leftChild >= nodes.size() || rightChild >= nodes.size()){
            valid = false;
            break;
        }
        const SIMDVector childMin = VectorMin(LoadFloat(nodes[leftChild].aabbMin), LoadFloat(nodes[rightChild].aabbMin));
        const SIMDVector childMax = VectorMax(LoadFloat(nodes[leftChild].aabbMax), LoadFloat(nodes[rightChild].aabbMax));
        if(
            !Vector3LessOrEqual(LoadFloat(nodes[i].aabbMin), VectorAdd(childMin, epsilonVector))
            || !Vector3GreaterOrEqual(LoadFloat(nodes[i].aabbMax), VectorSubtract(childMax, epsilonVector))
        )
            valid = false;
    }

    if(valid)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: scene BVH self-test PASSED ({} instances, {} nodes)"), instanceCount, nodeCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: scene BVH self-test FAILED"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

