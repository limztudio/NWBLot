// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/shared/renderer_state.h>

#include <core/alloc/scratch.h>
#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_triangle_indices.h>
#include <impl/assets_mesh/meshlet_vertex_attributes.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_RayTracingReconstructionScratchPaddingBytes = 4096u;

void CaptureRayTracingResourceSnapshot(
    const MeshResources& mesh,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
){
    outSnapshot = {
        .meshName = mesh.meshName,
        .positionBuffer = mesh.positionBuffer,
        .triangleIndexBuffer = mesh.triangleIndexBuffer,
        .attributeBuffer = mesh.attributeBuffer,
        .blas = mesh.blas,
        .swBvhPositionHeapHandle = mesh.swBvhPositionHeapHandle,
        .swBvhTriangleIndexHeapHandle = mesh.swBvhTriangleIndexHeapHandle,
        .swBvhNodeBuffer = mesh.swBvhNodeBuffer,
        .swBvhParentBuffer = mesh.swBvhParentBuffer,
        .swBvhNodeHeapHandle = mesh.swBvhNodeHeapHandle,
        .swBvhParentHeapHandle = mesh.swBvhParentHeapHandle,
        .meshletPrimitiveIndexCount = mesh.meshletPrimitiveIndexCount,
        .blasRefitsSinceRebuild = mesh.blasRefitsSinceRebuild,
        .swBvhRefitsSinceRebuild = mesh.swBvhRefitsSinceRebuild,
        .runtimeMesh = mesh.runtimeMesh,
        .blasBuildPending = mesh.blasBuildPending,
        .blasBackingFresh = mesh.blasBackingFresh,
        .blasBackingStateHandoffPending = mesh.blasBackingStateHandoffPending,
        .swBvhBuildPending = mesh.swBvhBuildPending,
        .swBvhTopologyBuilt = mesh.swBvhTopologyBuilt,
        .runtimeMeshVersion = mesh.runtimeMeshVersion,
        .csgLocalBounds = mesh.csgLocalBounds,
    };
}

[[nodiscard]] bool RayTracingResourceSnapshotMatches(
    const MeshResources& mesh,
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& snapshot
){
    return
        mesh.meshName == snapshot.meshName
        && mesh.positionBuffer.get() == snapshot.positionBuffer.get()
        && mesh.triangleIndexBuffer.get() == snapshot.triangleIndexBuffer.get()
        && mesh.attributeBuffer.get() == snapshot.attributeBuffer.get()
        && mesh.blas.get() == snapshot.blas.get()
        && mesh.swBvhPositionHeapHandle == snapshot.swBvhPositionHeapHandle
        && mesh.swBvhTriangleIndexHeapHandle == snapshot.swBvhTriangleIndexHeapHandle
        && mesh.swBvhNodeBuffer.get() == snapshot.swBvhNodeBuffer.get()
        && mesh.swBvhParentBuffer.get() == snapshot.swBvhParentBuffer.get()
        && mesh.swBvhNodeHeapHandle == snapshot.swBvhNodeHeapHandle
        && mesh.swBvhParentHeapHandle == snapshot.swBvhParentHeapHandle
        && mesh.meshletPrimitiveIndexCount == snapshot.meshletPrimitiveIndexCount
        && mesh.blasRefitsSinceRebuild == snapshot.blasRefitsSinceRebuild
        && mesh.swBvhRefitsSinceRebuild == snapshot.swBvhRefitsSinceRebuild
        && mesh.runtimeMesh == snapshot.runtimeMesh
        && mesh.blasBuildPending == snapshot.blasBuildPending
        && mesh.blasBackingFresh == snapshot.blasBackingFresh
        && mesh.blasBackingStateHandoffPending == snapshot.blasBackingStateHandoffPending
        && mesh.swBvhBuildPending == snapshot.swBvhBuildPending
        && mesh.swBvhTopologyBuilt == snapshot.swBvhTopologyBuilt
        && mesh.runtimeMeshVersion == snapshot.runtimeMeshVersion
        && mesh.csgLocalBounds.minBounds == snapshot.csgLocalBounds.minBounds
        && mesh.csgLocalBounds.maxBounds == snapshot.csgLocalBounds.maxBounds
    ;
}

[[nodiscard]] bool RayTracingResourceIdentityMatches(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& lhs,
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& rhs
){
    return
        lhs.meshName == rhs.meshName
        && lhs.positionBuffer.get() == rhs.positionBuffer.get()
        && lhs.triangleIndexBuffer.get() == rhs.triangleIndexBuffer.get()
        && lhs.attributeBuffer.get() == rhs.attributeBuffer.get()
        && lhs.swBvhPositionHeapHandle == rhs.swBvhPositionHeapHandle
        && lhs.swBvhTriangleIndexHeapHandle == rhs.swBvhTriangleIndexHeapHandle
        && lhs.meshletPrimitiveIndexCount == rhs.meshletPrimitiveIndexCount
        && lhs.runtimeMesh == rhs.runtimeMesh
        && lhs.runtimeMeshVersion == rhs.runtimeMeshVersion
        && lhs.csgLocalBounds.minBounds == rhs.csgLocalBounds.minBounds
        && lhs.csgLocalBounds.maxBounds == rhs.csgLocalBounds.maxBounds
    ;
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] static Core::BufferHandle SetupMeshBuffer(
    Core::Graphics& graphics,
    const Name& meshName,
    const AStringView suffix,
    const PayloadVector& payload,
    const tchar* label,
    const bool canHaveRawViews = false,
    const bool accelStructBuildInput = false
){
    const Name bufferName = DeriveName(meshName, suffix);
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive {} buffer name for mesh '{}'")
            , label
            , StringConvert(meshName.c_str())
        );
        return {};
    }

    Core::BufferHandle buffer;
    const RuntimeMeshBufferUpload::BufferSetupFailure::Enum failure = RuntimeMeshBufferUpload::SetupRequiredBuffer<PayloadT>(
        graphics,
        bufferName,
        payload,
        {
            false,
            canHaveRawViews,
            accelStructBuildInput,
            Core::ResourceQueueSharing::GraphicsAndAsyncCompute
        },
        buffer
    );
    switch(failure){
    case RuntimeMeshBufferUpload::BufferSetupFailure::None:
        return buffer;
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has empty {} payload")
            , StringConvert(meshName.c_str())
            , label
        );
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' {} payload byte size overflows")
            , StringConvert(meshName.c_str())
            , label
        );
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        break;
    }
    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create {} buffer for mesh '{}'")
        , label
        , StringConvert(meshName.c_str())
    );
    return {};
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] static bool AssignMeshBuffer(
    Core::Graphics& graphics,
    const Name& meshName,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const tchar* label,
    const bool canHaveRawViews = false,
    const bool accelStructBuildInput = false
){
    outBuffer = SetupMeshBuffer<PayloadT>(
        graphics,
        meshName,
        suffix,
        payload,
        label,
        canHaveRawViews,
        accelStructBuildInput
    );
    return outBuffer != nullptr;
}

template<typename PayloadVector>
[[nodiscard]] static bool AssignPaddedRawMeshBuffer(
    Core::Graphics& graphics,
    Core::Alloc::GlobalArena& arena,
    const Name& meshName,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const tchar* label
){
    outBuffer = nullptr;

    const Name bufferName = DeriveName(meshName, suffix);
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive {} buffer name for mesh '{}'")
            , label
            , StringConvert(meshName.c_str())
        );
        return false;
    }

    const RuntimeMeshBufferUpload::BufferSetupFailure::Enum failure =
        RuntimeMeshBufferUpload::SetupRequiredPaddedRawByteBuffer(
            graphics,
            arena,
            bufferName,
            payload,
            {
                false,
                true,
                false,
                Core::ResourceQueueSharing::GraphicsAndAsyncCompute
            },
            outBuffer
        )
    ;
    switch(failure){
    case RuntimeMeshBufferUpload::BufferSetupFailure::None:
        return true;
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has empty {} payload")
            , StringConvert(meshName.c_str())
            , label
        );
        return false;
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' {} payload byte size overflows")
            , StringConvert(meshName.c_str())
            , label
        );
        return false;
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create {} buffer for mesh '{}'")
            , label
            , StringConvert(meshName.c_str())
        );
        return false;
    }

    NWB_ASSERT(false);
    return false;
}

[[nodiscard]] static bool ValidateRawBufferLogicalByteCount(
    const Core::BufferHandle& buffer,
    const u32 logicalByteCount,
    const Name& meshName,
    const tchar* label
){
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no {} buffer")
            , StringConvert(meshName.c_str())
            , label
        );
        return false;
    }

    const Core::BufferDesc& desc = buffer->getCreationDescription();
    if(
        desc.structStride != sizeof(u8)
        || desc.byteSize < static_cast<u64>(logicalByteCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' {} buffer cannot cover {} logical bytes")
            , StringConvert(meshName.c_str())
            , label
            , logicalByteCount
        );
        return false;
    }
    return true;
}

template<typename PositionVector>
[[nodiscard]] static bool BuildPositionStreamBounds(const PositionVector& positions, CsgReceiverCpuBounds& outBounds){
    outBounds = CsgReceiverCpuBounds{};
    if(positions.empty())
        return false;

    SIMDVector minBounds;
    SIMDVector maxBounds;
    AabbTests::Reset(minBounds, maxBounds);
    for(const Float3U& position : positions)
        AabbTests::Expand(LoadFloat(position), minBounds, maxBounds);

    if(!AabbTests::Valid(minBounds, maxBounds))
        return false;

    StoreFloatInt(VectorSetW(minBounds, 0.0f), s_CsgBoundsValidFlag | s_CsgBoundsFiniteFlag, &outBounds.minBounds);
    StoreFloatInt(VectorSetW(maxBounds, 0.0f), 0, &outBounds.maxBounds);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMeshSystem::createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh){
    outMesh = nullptr;

    const Name meshPath = meshAsset.name();
    if(!meshPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer mesh is empty"));
        return false;
    }

    const auto foundMesh = m_meshState.m_meshes.find(meshPath);
    if(foundMesh != m_meshState.m_meshes.end()){
        if(!meshRenderBindingsReady(foundMesh.value())){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cached mesh '{}' is missing creation-time render bindings")
                , StringConvert(meshPath.c_str())
            );
            return false;
        }
        outMesh = &foundMesh.value();
        NWB_ASSERT(outMesh->valid());
        return true;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Mesh::AssetTypeName(), meshPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to load mesh '{}'"), StringConvert(meshPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Mesh::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: asset '{}' is not mesh"), StringConvert(meshPath.c_str()));
        return false;
    }

    const Mesh& mesh = static_cast<const Mesh&>(*loadedAsset);
    NWB_ASSERT(mesh.validatePayload());

    if(
        mesh.meshlets().size() > static_cast<usize>(Limit<u32>::s_Max)
        || mesh.meshletPrimitiveIndices().size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' meshlet payload exceeds u32 limits")
            , StringConvert(meshPath.c_str())
        );
        return false;
    }

    MeshResources createdMesh;
    createdMesh.meshName = meshPath;
    createdMesh.meshletCount = static_cast<u32>(mesh.meshlets().size());
    createdMesh.meshletPrimitiveIndexCount = static_cast<u32>(mesh.meshletPrimitiveIndices().size());
    if(!__hidden_mesh::BuildPositionStreamBounds(mesh.positionStream(), createdMesh.csgLocalBounds)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has invalid CSG receiver bounds")
            , StringConvert(meshPath.c_str())
        );
        return false;
    }

    const bool rtSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct);
    // Without hardware ray tracing the software BVH shadow fallback runs instead; it reads positions and the
    // reconstructed triangle indices as raw byte buffers, so those buffers need raw views in that case.
    const bool swShadow = !rtSupported;

    bool uploaded = true;
    uploaded = __hidden_mesh::AssignMeshBuffer<Float3U>(
        m_graphics,
        meshPath,
        createdMesh.positionBuffer,
        AStringView(":positions"),
        mesh.positionStream(),
        NWB_TEXT("position"),
        swShadow,
        rtSupported
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.normalBuffer,
        AStringView(":normals"),
        mesh.normalStream(),
        NWB_TEXT("normal")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.tangentBuffer,
        AStringView(":tangents"),
        mesh.tangentStream(),
        NWB_TEXT("tangent")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Float2U>(
        m_graphics,
        meshPath,
        createdMesh.uv0Buffer,
        AStringView(":uv0"),
        mesh.uv0Stream(),
        NWB_TEXT("uv0")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.colorBuffer,
        AStringView(":colors"),
        mesh.colorStream(),
        NWB_TEXT("color")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<MeshletDesc>(
        m_graphics,
        meshPath,
        createdMesh.meshletDescBuffer,
        AStringView(":meshlets"),
        mesh.meshlets(),
        NWB_TEXT("meshlet descriptor")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<MeshletBounds>(
        m_graphics,
        meshPath,
        createdMesh.meshletBoundsBuffer,
        AStringView(":meshlet_bounds"),
        mesh.meshletBounds(),
        NWB_TEXT("meshlet bounds"),
        true
    ) && uploaded;
    uploaded = __hidden_mesh::AssignPaddedRawMeshBuffer(
        m_graphics,
        m_arena,
        meshPath,
        createdMesh.meshletPositionRefDeltaBuffer,
        AStringView(":meshlet_position_ref_deltas"),
        mesh.meshletPositionRefDeltas(),
        NWB_TEXT("meshlet position ref delta")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignPaddedRawMeshBuffer(
        m_graphics,
        m_arena,
        meshPath,
        createdMesh.meshletAttributeRefDeltaBuffer,
        AStringView(":meshlet_attribute_ref_deltas"),
        mesh.meshletAttributeRefDeltas(),
        NWB_TEXT("meshlet attribute ref delta")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<MeshletLocalVertexRef>(
        m_graphics,
        meshPath,
        createdMesh.meshletLocalVertexRefBuffer,
        AStringView(":meshlet_local_vertex_refs"),
        mesh.meshletLocalVertexRefs(),
        NWB_TEXT("meshlet local vertex ref")
    ) && uploaded;
    uploaded = __hidden_mesh::AssignPaddedRawMeshBuffer(
        m_graphics,
        m_arena,
        meshPath,
        createdMesh.meshletPrimitiveIndexBuffer,
        AStringView(":meshlet_primitive_indices"),
        mesh.meshletPrimitiveIndices(),
        NWB_TEXT("meshlet primitive index")
    ) && uploaded;
    if(!uploaded)
        return false;

    // Both shadow backends trace triangles, so the reconstructed index buffer is always created. The
    // hardware path consumes it as an accel-struct build input; the software fallback reads it as a raw
    // byte buffer. blasBuildPending / swBvhBuildPending route the mesh to whichever backend is active.
    {
        const usize indexCount = static_cast<usize>(createdMesh.meshletPrimitiveIndexCount);
        Core::Alloc::ScratchArena scratchArena(
            RendererArenaScope::s_RayTracingBuildArena,
            indexCount * sizeof(u32) + __hidden_mesh::s_RayTracingReconstructionScratchPaddingBytes
        );
        Vector<u32, Core::Alloc::ScratchArena> triangleIndices{ scratchArena };
        triangleIndices.reserve(indexCount);
        if(!BuildMeshletTriangleIndices(mesh, triangleIndices)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to reconstruct shadow trace triangle indices for mesh '{}'")
                , StringConvert(meshPath.c_str())
            );
            return false;
        }

        const Name indexBufferName = DeriveName(meshPath, AStringView(":rt_triangle_indices"));
        if(!indexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive shadow trace index buffer name for mesh '{}'")
                , StringConvert(meshPath.c_str())
            );
            return false;
        }

        RuntimeMeshBufferUpload::BufferFlags indexFlags;
        indexFlags.canHaveRawViews = swShadow;
        indexFlags.accelStructBuildInput = rtSupported;
        // The dedicated async shadow packet reads this reconstructed stream alongside the Graphics-side build
        // and raster packets. Keep the sharing contract consistent with the other shadow trace inputs so it
        // never needs an ownership transfer during the Graphics -> Compute overlap.
        indexFlags.queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute;
        const RuntimeMeshBufferUpload::BufferSetupFailure::Enum indexFailure = RuntimeMeshBufferUpload::SetupRequiredBuffer<u32>(
            m_graphics,
            indexBufferName,
            triangleIndices,
            indexFlags,
            createdMesh.triangleIndexBuffer
        );
        if(indexFailure != RuntimeMeshBufferUpload::BufferSetupFailure::None){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow trace index buffer for mesh '{}'")
                , StringConvert(meshPath.c_str())
            );
            return false;
        }

        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: mesh '{}' shadow trace index buffer ready ({} indices, expected {})")
            , StringConvert(meshPath.c_str())
            , static_cast<u64>(triangleIndices.size())
            , static_cast<u64>(createdMesh.meshletPrimitiveIndexCount)
        );

        createdMesh.blasBuildPending = rtSupported;
        // The software BVH is built for the no-RT fallback AND, on RT hardware, for the HYBRID transparent shadow (the
        // HW pass casts opaque shadows; the SW traversal casts the colored transparent shadow). The positions/indices
        // already carry raw views on RT hardware (as accel-struct build inputs), so only the build itself is gated here;
        // buildPendingMeshSwBvh only actually runs on RT hardware when the scene holds a transparent occluder.
        createdMesh.swBvhBuildPending = swShadow || rtSupported;
    }

    // Flat per-triangle-corner shadow/caustic trace attribute buffer, indexed as primitive*3+corner in lockstep
    // with the reconstructed triangle index buffer above. This preserves raster normal semantics: smooth edges share
    // normal refs, hard edges carry separate refs even when the position is shared. Built unconditionally alongside
    // the index buffer (neither backend is known at mesh-creation time); both shadow backends read it as a
    // ByteAddressBuffer, so it always carries a raw view; the structured stride also exposes a plain SRV.
    {
        const usize attributeCount = mesh.meshletPrimitiveIndices().size();
        Core::Alloc::ScratchArena scratchArena(
            RendererArenaScope::s_RayTracingAttributeArena,
            attributeCount * sizeof(AttribGpu) + __hidden_mesh::s_RayTracingReconstructionScratchPaddingBytes
        );
        Vector<AttribGpu, Core::Alloc::ScratchArena> triangleAttributes{ scratchArena };
        if(!BuildMeshletTriangleAttributes(mesh, triangleAttributes)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to reconstruct shadow trace triangle attributes for mesh '{}'")
                , StringConvert(meshPath.c_str())
            );
            return false;
        }

        const Name attributeBufferName = DeriveName(meshPath, AStringView(":rt_triangle_attributes"));
        if(!attributeBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive shadow trace attribute buffer name for mesh '{}'")
                , StringConvert(meshPath.c_str())
            );
            return false;
        }

        RuntimeMeshBufferUpload::BufferFlags attributeFlags;
        attributeFlags.canHaveRawViews = true;
        // Hybrid transparent shadow tracing consumes the same corner attributes on AsyncCompute after Graphics
        // has prepared the frame, so this trace-only stream is a shared read input too.
        attributeFlags.queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute;
        const RuntimeMeshBufferUpload::BufferSetupFailure::Enum attributeFailure = RuntimeMeshBufferUpload::SetupRequiredBuffer<AttribGpu>(
            m_graphics,
            attributeBufferName,
            triangleAttributes,
            attributeFlags,
            createdMesh.attributeBuffer
        );
        if(attributeFailure != RuntimeMeshBufferUpload::BufferSetupFailure::None){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow trace triangle attribute buffer for mesh '{}'")
                , StringConvert(meshPath.c_str())
            );
            return false;
        }
    }

    NWB_ASSERT(createdMesh.valid());
    if(!createMeshRenderBindings(createdMesh))
        return false;

    auto result = m_meshState.m_meshes.try_emplace(meshPath, Move(createdMesh));
    auto it = result.first;

    outMesh = &it.value();
    NWB_ASSERT(outMesh->valid());
    return true;
}

bool RendererMeshSystem::findMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh){
    outMesh = nullptr;

    const Name meshPath = meshAsset.name();
    return findMeshResources(meshPath, outMesh);
}

bool RendererMeshSystem::findMeshResources(const Name& meshKey, MeshResources*& outMesh){
    outMesh = nullptr;
    if(!meshKey)
        return false;

    const auto foundMesh = m_meshState.m_meshes.find(meshKey);
    if(foundMesh == m_meshState.m_meshes.end())
        return false;

    if(!meshRenderBindingsReady(foundMesh.value()))
        return false;

    outMesh = &foundMesh.value();
    NWB_ASSERT(outMesh->valid());
    return true;
}

bool RendererMeshSystem::createRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh){
    outMesh = nullptr;

    NWB_ASSERT(desc.valid());

    const auto foundMesh = m_meshState.m_meshes.find(desc.meshKey);
    if(foundMesh != m_meshState.m_meshes.end()){
        if(!foundMesh.value().runtimeMesh){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime mesh '{}' collides with a static mesh resource")
                , StringConvert(desc.meshKey.c_str())
            );
            return false;
        }
        if(foundMesh.value().runtimeMeshVersion != desc.version){
            releaseMeshGeometryHeapHandles(foundMesh.value());
            m_meshState.m_meshes.erase(foundMesh);
        }
        else{
            if(!meshRenderBindingsReady(foundMesh.value())){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cached runtime mesh '{}' is missing creation-time render bindings")
                    , StringConvert(desc.meshKey.c_str())
                );
                return false;
            }
            outMesh = &foundMesh.value();
            NWB_ASSERT(outMesh->valid());
            return true;
        }
    }

    MeshResources createdMesh;
    createdMesh.meshName = desc.meshKey;
    createdMesh.positionBuffer = desc.positionBuffer;
    createdMesh.normalBuffer = desc.normalBuffer;
    createdMesh.tangentBuffer = desc.tangentBuffer;
    createdMesh.uv0Buffer = desc.uv0Buffer;
    createdMesh.colorBuffer = desc.colorBuffer;
    createdMesh.meshletDescBuffer = desc.meshletDescBuffer;
    createdMesh.meshletBoundsBuffer = desc.meshletBoundsBuffer;
    createdMesh.meshletPositionRefDeltaBuffer = desc.meshletPositionRefDeltaBuffer;
    createdMesh.meshletAttributeRefDeltaBuffer = desc.meshletAttributeRefDeltaBuffer;
    createdMesh.meshletLocalVertexRefBuffer = desc.meshletLocalVertexRefBuffer;
    createdMesh.meshletPrimitiveIndexBuffer = desc.meshletPrimitiveIndexBuffer;
    createdMesh.triangleIndexBuffer = desc.triangleIndexBuffer;
    createdMesh.attributeBuffer = desc.attributeBuffer;
    createdMesh.blasBuildPending = (desc.triangleIndexBuffer != nullptr);
    createdMesh.meshletCount = desc.meshletCount;
    createdMesh.meshletPrimitiveIndexCount = desc.meshletPrimitiveIndexCount;
    createdMesh.runtimeMesh = true;
    createdMesh.dynamicMeshletBoundsFresh = desc.dynamicMeshletBoundsFresh;
    createdMesh.dynamicMeshletConesFresh = desc.dynamicMeshletConesFresh;
    createdMesh.runtimeMeshVersion = desc.version;
    NWB_ASSERT(desc.localBounds.valid());
    createdMesh.csgLocalBounds.minBounds = desc.localBounds.minBounds;
    createdMesh.csgLocalBounds.maxBounds = desc.localBounds.maxBounds;
    createdMesh.csgLocalBounds.minBounds.w = s_CsgBoundsValidFlag;
    if(desc.localBounds.finite())
        createdMesh.csgLocalBounds.minBounds.w |= s_CsgBoundsFiniteFlag;
    createdMesh.csgLocalBounds.maxBounds.w = 0;
    if((createdMesh.meshletPrimitiveIndexCount % s_MeshletTriangleIndexCount) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime mesh '{}' has {} primitive-index bytes, which cannot form triangles")
            , StringConvert(createdMesh.meshName.c_str())
            , createdMesh.meshletPrimitiveIndexCount
        );
        return false;
    }
    if(!__hidden_mesh::ValidateRawBufferLogicalByteCount(
        createdMesh.meshletPrimitiveIndexBuffer,
        createdMesh.meshletPrimitiveIndexCount,
        createdMesh.meshName,
        NWB_TEXT("meshlet primitive index")
    ))
        return false;
    NWB_ASSERT(createdMesh.valid());
    if(!createMeshRenderBindings(createdMesh))
        return false;

    auto result = m_meshState.m_meshes.try_emplace(desc.meshKey, Move(createdMesh));
    auto it = result.first;

    outMesh = &it.value();
    NWB_ASSERT(outMesh->valid());
    return true;
}

bool RendererMeshSystem::findRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh){
    outMesh = nullptr;
    NWB_ASSERT(desc.valid());

    const auto foundMesh = m_meshState.m_meshes.find(desc.meshKey);
    if(foundMesh == m_meshState.m_meshes.end())
        return false;

    MeshResources& mesh = foundMesh.value();
    if(!mesh.runtimeMesh || mesh.runtimeMeshVersion != desc.version)
        return false;

    if(!meshRenderBindingsReady(mesh))
        return false;

    outMesh = &mesh;
    NWB_ASSERT(outMesh->valid());
    return true;
}

void RendererMeshSystem::pruneRuntimeMeshResources(){
    if(m_meshState.m_meshes.empty())
        return;

    const auto* meshSystem = m_world.getSystem<NWB::Impl::MeshSystem>();
    for(auto it = m_meshState.m_meshes.begin(); it != m_meshState.m_meshes.end();){
        const MeshResources& mesh = it.value();
        if(!mesh.runtimeMesh){
            ++it;
            continue;
        }

        if(meshSystem && meshSystem->containsRuntimeMesh(mesh.meshName, mesh.runtimeMeshVersion)){
            ++it;
            continue;
        }

        releaseMeshGeometryHeapHandles(it.value());
        it = m_meshState.m_meshes.erase(it);
    }
}

void RendererMeshSystem::collectRayTracingResourceSnapshots(
    ECSRenderDetail::MeshRayTracingResourceSnapshotVector& outSnapshots
)const{
    outSnapshots.clear();
    outSnapshots.reserve(m_meshState.m_meshes.size());
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        ECSRenderDetail::MeshRayTracingResourceSnapshot snapshot;
        __hidden_mesh::CaptureRayTracingResourceSnapshot(meshIt.value(), snapshot);
        outSnapshots.push_back(Move(snapshot));
    }
}

bool RendererMeshSystem::findRayTracingResourceSnapshot(
    const Name& meshName,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
)const{
    outSnapshot = {};
    const auto found = m_meshState.m_meshes.find(meshName);
    if(found == m_meshState.m_meshes.end())
        return false;

    __hidden_mesh::CaptureRayTracingResourceSnapshot(found.value(), outSnapshot);
    return true;
}

bool RendererMeshSystem::findRenderableRayTracingResourceSnapshot(
    const RenderableMeshDesc& desc,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
)const{
    outSnapshot = {};
    if(!desc.valid())
        return false;

    const Name meshName = desc.runtime ? desc.runtimeMesh.meshKey : desc.mesh.name();
    const auto found = m_meshState.m_meshes.find(meshName);
    if(found == m_meshState.m_meshes.end())
        return false;

    const MeshResources& mesh = found.value();
    if(
        (desc.runtime && (!mesh.runtimeMesh || mesh.runtimeMeshVersion != desc.runtimeMesh.version))
        || !meshRenderBindingsReady(mesh)
    )
        return false;

    __hidden_mesh::CaptureRayTracingResourceSnapshot(mesh, outSnapshot);
    return true;
}

bool RendererMeshSystem::commitRayTracingResourceSnapshot(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& desired
){
    const auto found = m_meshState.m_meshes.find(expected.meshName);
    const bool currentMatches = found != m_meshState.m_meshes.end()
        && __hidden_mesh::RayTracingResourceSnapshotMatches(found.value(), expected)
    ;
    if(!currentMatches || !__hidden_mesh::RayTracingResourceIdentityMatches(expected, desired)){
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        const Core::GpuDescriptorHandle currentNodeHandle = found != m_meshState.m_meshes.end()
            ? found.value().swBvhNodeHeapHandle
            : Core::GpuDescriptorHandle::invalid()
        ;
        const Core::GpuDescriptorHandle currentParentHandle = found != m_meshState.m_meshes.end()
            ? found.value().swBvhParentHeapHandle
            : Core::GpuDescriptorHandle::invalid()
        ;
        const Core::GpuDescriptorHandle candidateNodeHandle = desired.swBvhNodeHeapHandle;
        if(
            heap.isInitialized()
            && candidateNodeHandle.valid()
            && candidateNodeHandle != expected.swBvhNodeHeapHandle
            && candidateNodeHandle != expected.swBvhParentHeapHandle
            && candidateNodeHandle != currentNodeHandle
            && candidateNodeHandle != currentParentHandle
        ){
            heap.free(candidateNodeHandle);
            desired.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
            if(desired.swBvhParentHeapHandle == candidateNodeHandle)
                desired.swBvhParentHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        const Core::GpuDescriptorHandle candidateParentHandle = desired.swBvhParentHeapHandle;
        if(
            heap.isInitialized()
            && candidateParentHandle.valid()
            && candidateParentHandle != expected.swBvhNodeHeapHandle
            && candidateParentHandle != expected.swBvhParentHeapHandle
            && candidateParentHandle != currentNodeHandle
            && candidateParentHandle != currentParentHandle
        ){
            heap.free(candidateParentHandle);
            desired.swBvhParentHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        return false;
    }

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        heap.isInitialized()
        && expected.swBvhNodeHeapHandle.valid()
        && expected.swBvhNodeHeapHandle != desired.swBvhNodeHeapHandle
        && expected.swBvhNodeHeapHandle != desired.swBvhParentHeapHandle
    )
        heap.free(expected.swBvhNodeHeapHandle);
    if(
        heap.isInitialized()
        && expected.swBvhParentHeapHandle.valid()
        && expected.swBvhParentHeapHandle != expected.swBvhNodeHeapHandle
        && expected.swBvhParentHeapHandle != desired.swBvhNodeHeapHandle
        && expected.swBvhParentHeapHandle != desired.swBvhParentHeapHandle
    )
        heap.free(expected.swBvhParentHeapHandle);

    MeshResources& mesh = found.value();
    mesh.blas = desired.blas;
    mesh.swBvhNodeBuffer = desired.swBvhNodeBuffer;
    mesh.swBvhParentBuffer = desired.swBvhParentBuffer;
    mesh.swBvhNodeHeapHandle = desired.swBvhNodeHeapHandle;
    mesh.swBvhParentHeapHandle = desired.swBvhParentHeapHandle;
    mesh.blasRefitsSinceRebuild = desired.blasRefitsSinceRebuild;
    mesh.swBvhRefitsSinceRebuild = desired.swBvhRefitsSinceRebuild;
    mesh.blasBuildPending = desired.blasBuildPending;
    mesh.blasBackingFresh = desired.blasBackingFresh;
    mesh.blasBackingStateHandoffPending = desired.blasBackingStateHandoffPending;
    mesh.swBvhBuildPending = desired.swBvhBuildPending;
    mesh.swBvhTopologyBuilt = desired.swBvhTopologyBuilt;
    return true;
}

bool RendererMeshSystem::ensureRayTracingInputHeapHandles(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
){
    outSnapshot = {};
    const auto found = m_meshState.m_meshes.find(expected.meshName);
    if(
        found == m_meshState.m_meshes.end()
        || !__hidden_mesh::RayTracingResourceSnapshotMatches(found.value(), expected)
        || !ensureMeshSwBvhInputHeapHandles(found.value())
    )
        return false;

    __hidden_mesh::CaptureRayTracingResourceSnapshot(found.value(), outSnapshot);
    return true;
}

void RendererMeshSystem::confirmAcceptedRayTracingStateHandoffs()noexcept{
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        MeshResources& mesh = meshIt.value();
        if(mesh.blasBackingFresh && mesh.blasBackingStateHandoffPending){
            mesh.blasBackingFresh = false;
            mesh.blasBackingStateHandoffPending = false;
        }
    }
}

void RendererMeshSystem::discardRayTracingBuildState()noexcept{
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        MeshResources& mesh = meshIt.value();
        mesh.blasBackingStateHandoffPending = false;
        if(mesh.blas)
            mesh.blasBuildPending = true;
        if(mesh.swBvhNodeBuffer || mesh.swBvhParentBuffer){
            mesh.swBvhBuildPending = true;
            mesh.swBvhTopologyBuilt = false;
        }
        mesh.blasRefitsSinceRebuild = 0u;
        mesh.swBvhRefitsSinceRebuild = 0u;
    }
}

bool RendererMeshSystem::collectSoftwareBvhParentBuildStates(ECSRenderDetail::MeshSoftwareBvhParentBuildStateVector& outStates)const{
    outStates.clear();
    outStates.reserve(m_meshState.m_meshes.size());
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        if(!mesh.swBvhNodeBuffer && !mesh.swBvhParentBuffer)
            continue;
        if(!mesh.swBvhNodeBuffer || !mesh.swBvhParentBuffer){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererMeshSystem: incomplete software BVH state for a live mesh"));
            return false;
        }
        outStates.push_back({
            .buffer = mesh.swBvhParentBuffer,
            .identity = DeriveName(mesh.meshName, AStringView(":shadow_trace_sw_parent")),
        });
    }
    return true;
}

void RendererMeshSystem::collectRetainedAccelerationStateBuffers(ECSRenderDetail::MeshRetainedAccelerationStateBufferVector& outBuffers)const{
    outBuffers.clear();
    outBuffers.reserve(m_meshState.m_meshes.size() * 5u);
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        // A frozen BLAS plan can fall back to the native current-mesh build when runtime geometry changes between
        // preflight and recording. Retain every live source pair and backing buffer, not only the frozen inputs.
        if(mesh.blas){
            outBuffers.push_back(mesh.positionBuffer);
            outBuffers.push_back(mesh.triangleIndexBuffer);
            outBuffers.push_back(mesh.blas->getBackingBufferHandle());
        }
        // Preserve both mesh-local SW state buffers across route changes. A hybrid native build may leave these UAVs
        // before the next frame switches to software-only frozen recording.
        outBuffers.push_back(mesh.swBvhNodeBuffer);
        outBuffers.push_back(mesh.swBvhParentBuffer);
    }
}

void RendererMeshSystem::collectBlasGraphStates(ECSRenderDetail::MeshBlasGraphStateVector& outStates)const{
    outStates.clear();
    outStates.reserve(m_meshState.m_meshes.size());
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        if(!mesh.blas)
            continue;
        outStates.push_back({
            .meshName = mesh.meshName,
            .blas = mesh.blas,
            .backingFresh = mesh.blasBackingFresh,
            .nativeBuildsBlas = mesh.runtimeMesh || mesh.blasBuildPending,
        });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

