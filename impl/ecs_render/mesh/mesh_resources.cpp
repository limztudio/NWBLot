// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"

#include "runtime_mesh_pruning.h"

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>

#include <core/alloc/scratch.h>
#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
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
inline constexpr Name s_RuntimeMeshPruningArena("impl/ecs_render/runtime_mesh_pruning");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool RuntimeMeshSourceMatches(const MeshResources& mesh, const RuntimeMeshDesc& desc){
    return
        mesh.runtimeMeshVersion == desc.version
        && mesh.positionBuffer == desc.positionBuffer
        && mesh.normalBuffer == desc.normalBuffer
        && mesh.tangentBuffer == desc.tangentBuffer
        && mesh.uv0Buffer == desc.uv0Buffer
        && mesh.colorBuffer == desc.colorBuffer
        && mesh.meshletDescBuffer == desc.meshletDescBuffer
        && mesh.meshletBoundsBuffer == desc.meshletBoundsBuffer
        && mesh.meshletPositionRefDeltaBuffer == desc.meshletPositionRefDeltaBuffer
        && mesh.meshletAttributeRefDeltaBuffer == desc.meshletAttributeRefDeltaBuffer
        && mesh.meshletLocalVertexRefBuffer == desc.meshletLocalVertexRefBuffer
        && mesh.meshletPrimitiveIndexBuffer == desc.meshletPrimitiveIndexBuffer
        && mesh.triangleIndexBuffer == desc.triangleIndexBuffer
        && mesh.attributeBuffer == desc.attributeBuffer
        && mesh.meshletCount == desc.meshletCount
        && mesh.meshletPrimitiveIndexCount == desc.meshletPrimitiveIndexCount
    ;
}

static void RefreshRuntimeMeshContent(MeshResources& mesh, const RuntimeMeshDesc& desc){
    mesh.runtimeGeometryContentRevision = desc.geometryContentRevision;
    mesh.dynamicMeshletBoundsFresh = desc.dynamicMeshletBoundsFresh;
    mesh.dynamicMeshletConesFresh = desc.dynamicMeshletConesFresh;
    mesh.csgLocalBounds.minBounds = desc.localBounds.minBounds;
    mesh.csgLocalBounds.maxBounds = desc.localBounds.maxBounds;
    mesh.csgLocalBounds.minBounds.w = s_CsgBoundsValidFlag;
    if(desc.localBounds.finite())
        mesh.csgLocalBounds.minBounds.w |= s_CsgBoundsFiniteFlag;
    mesh.csgLocalBounds.maxBounds.w = 0;
}

[[nodiscard]] static bool ReportMeshBufferSetupFailure(
    const RuntimeMeshBufferUpload::BufferSetupFailure::Enum failure,
    const Name& meshName,
    const NotNull<const tchar*> label
){
    switch(failure){
    case RuntimeMeshBufferUpload::BufferSetupFailure::None:
        return true;
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has empty {} payload")
            , StringConvert(meshName.c_str())
            , label.get()
        );
        return false;
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' {} payload byte size overflows")
            , StringConvert(meshName.c_str())
            , label.get()
        );
        return false;
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        break;
    }
    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create {} buffer for mesh '{}'")
        , label.get()
        , StringConvert(meshName.c_str())
    );
    return false;
}

[[nodiscard]] static Name DeriveMeshBufferName(const Name& meshName, const AStringView suffix, const NotNull<const tchar*> label){
    const Name bufferName = DeriveName(meshName, suffix);
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive {} buffer name for mesh '{}'")
            , label.get()
            , StringConvert(meshName.c_str())
        );
    }
    return bufferName;
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] static Core::BufferHandle SetupMeshBuffer(
    Core::GraphicsRuntime& graphics,
    const Name& meshName,
    const AStringView suffix,
    const PayloadVector& payload,
    const NotNull<const tchar*> label,
    const bool canHaveRawViews = false,
    const bool accelStructBuildInput = false
){
    const Name bufferName = DeriveMeshBufferName(meshName, suffix, label);
    if(!bufferName)
        return {};

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
    if(!ReportMeshBufferSetupFailure(failure, meshName, label))
        return {};
    return buffer;
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] static bool AssignMeshBuffer(
    Core::GraphicsRuntime& graphics,
    const Name& meshName,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const NotNull<const tchar*> label,
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
    Core::GraphicsRuntime& graphics,
    Core::Alloc::GlobalArena& arena,
    const Name& meshName,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const NotNull<const tchar*> label
){
    outBuffer = nullptr;

    const Name bufferName = DeriveMeshBufferName(meshName, suffix, label);
    if(!bufferName)
        return false;

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
    if(failure == RuntimeMeshBufferUpload::BufferSetupFailure::None)
        return true;

    return ReportMeshBufferSetupFailure(failure, meshName, label);
}

[[nodiscard]] static bool ValidateRawBufferLogicalByteCount(
    const Core::BufferHandle& buffer,
    const u32 logicalByteCount,
    const Name& meshName,
    const NotNull<const tchar*> label
){
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no {} buffer")
            , StringConvert(meshName.c_str())
            , label.get()
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
            , label.get()
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

    StoreFloatInt(VectorSetW(minBounds, 0.0f), s_CsgBoundsValidFlag | s_CsgBoundsFiniteFlag, outBounds.minBounds);
    StoreFloatInt(VectorSetW(maxBounds, 0.0f), 0, outBounds.maxBounds);
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
        NWB_ASSERT(meshRenderBindingsReady(foundMesh.value()));
        if(!meshRenderBindingsReady(foundMesh.value()))
            return false;
        outMesh = &foundMesh.value();
        NWB_ASSERT(outMesh->valid());
        return true;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    const Mesh* loadedMesh = m_assetManager.loadTypedSync<Mesh>(
        meshPath,
        loadedAsset,
        MakeNotNull(NWB_TEXT("RendererMeshSystem::createMeshResources")),
        MakeNotNull(NWB_TEXT("RendererSystem")),
        MakeNotNull("mesh")
    );
    if(!loadedMesh)
        return false;

    const Mesh& mesh = *loadedMesh;
    // Mesh::loadBinary already ran the full payload validation (including u32 stream limits).
    NWB_ASSERT(mesh.validatePayload());
    NWB_ASSERT(mesh.meshlets().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(mesh.meshletPrimitiveIndices().size() <= static_cast<usize>(Limit<u32>::s_Max));

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
    // Both tracing backends read raw position/index buffers for material evaluation.
    const bool swShadow = !rtSupported;

    bool uploaded = true;
    uploaded = __hidden_mesh::AssignMeshBuffer<Float3U>(
        m_graphics,
        meshPath,
        createdMesh.positionBuffer,
        AStringView(":positions"),
        mesh.positionStream(),
        MakeNotNull(NWB_TEXT("position")),
        true,
        rtSupported
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.normalBuffer,
        AStringView(":normals"),
        mesh.normalStream(),
        MakeNotNull(NWB_TEXT("normal"))
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.tangentBuffer,
        AStringView(":tangents"),
        mesh.tangentStream(),
        MakeNotNull(NWB_TEXT("tangent"))
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Float2U>(
        m_graphics,
        meshPath,
        createdMesh.uv0Buffer,
        AStringView(":uv0"),
        mesh.uv0Stream(),
        MakeNotNull(NWB_TEXT("uv0"))
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<Half4U>(
        m_graphics,
        meshPath,
        createdMesh.colorBuffer,
        AStringView(":colors"),
        mesh.colorStream(),
        MakeNotNull(NWB_TEXT("color"))
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<MeshletDesc>(
        m_graphics,
        meshPath,
        createdMesh.meshletDescBuffer,
        AStringView(":meshlets"),
        mesh.meshlets(),
        MakeNotNull(NWB_TEXT("meshlet descriptor"))
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<MeshletBounds>(
        m_graphics,
        meshPath,
        createdMesh.meshletBoundsBuffer,
        AStringView(":meshlet_bounds"),
        mesh.meshletBounds(),
        MakeNotNull(NWB_TEXT("meshlet bounds")),
        true
    ) && uploaded;
    uploaded = __hidden_mesh::AssignPaddedRawMeshBuffer(
        m_graphics,
        m_arena,
        meshPath,
        createdMesh.meshletPositionRefDeltaBuffer,
        AStringView(":meshlet_position_ref_deltas"),
        mesh.meshletPositionRefDeltas(),
        MakeNotNull(NWB_TEXT("meshlet position ref delta"))
    ) && uploaded;
    uploaded = __hidden_mesh::AssignPaddedRawMeshBuffer(
        m_graphics,
        m_arena,
        meshPath,
        createdMesh.meshletAttributeRefDeltaBuffer,
        AStringView(":meshlet_attribute_ref_deltas"),
        mesh.meshletAttributeRefDeltas(),
        MakeNotNull(NWB_TEXT("meshlet attribute ref delta"))
    ) && uploaded;
    uploaded = __hidden_mesh::AssignMeshBuffer<MeshletLocalVertexRef>(
        m_graphics,
        meshPath,
        createdMesh.meshletLocalVertexRefBuffer,
        AStringView(":meshlet_local_vertex_refs"),
        mesh.meshletLocalVertexRefs(),
        MakeNotNull(NWB_TEXT("meshlet local vertex ref"))
    ) && uploaded;
    uploaded = __hidden_mesh::AssignPaddedRawMeshBuffer(
        m_graphics,
        m_arena,
        meshPath,
        createdMesh.meshletPrimitiveIndexBuffer,
        AStringView(":meshlet_primitive_indices"),
        mesh.meshletPrimitiveIndices(),
        MakeNotNull(NWB_TEXT("meshlet primitive index"))
    ) && uploaded;
    if(!uploaded)
        return false;

    // Both shadow backends trace triangles; always create the reconstructed index buffer.
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
        indexFlags.canHaveRawViews = true;
        indexFlags.accelStructBuildInput = rtSupported;
        // Async shadow packet shares this stream; keep sharing consistent to avoid ownership transfer.
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
        // Keep software geometry pending until a software preparation route accepts its build.
        createdMesh.swBvhBuildPending = swShadow || rtSupported;
    }

    // Flat per-corner trace attributes in lockstep with the index buffer; always carries a raw view.
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
        // Tracing shares these attributes on AsyncCompute; keep shared-read input.
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
    NWB_ASSERT(meshKey);
    if(!meshKey)
        return false;

    const auto foundMesh = m_meshState.m_meshes.find(meshKey);
    if(foundMesh == m_meshState.m_meshes.end())
        return false;

    NWB_ASSERT(meshRenderBindingsReady(foundMesh.value()));
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
        if(!__hidden_mesh::RuntimeMeshSourceMatches(foundMesh.value(), desc)){
            releaseMeshGeometryHeapHandles(foundMesh.value());
            m_meshState.m_meshes.erase(foundMesh);
        }
        else{
            NWB_ASSERT(meshRenderBindingsReady(foundMesh.value()));
            if(!meshRenderBindingsReady(foundMesh.value()))
                return false;
            outMesh = &foundMesh.value();
            __hidden_mesh::RefreshRuntimeMeshContent(*outMesh, desc);
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
    createdMesh.runtimeMeshVersion = desc.version;
    NWB_ASSERT(desc.localBounds.valid());
    __hidden_mesh::RefreshRuntimeMeshContent(createdMesh, desc);
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
        MakeNotNull(NWB_TEXT("meshlet primitive index"))
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
    if(!mesh.runtimeMesh || !__hidden_mesh::RuntimeMeshSourceMatches(mesh, desc))
        return false;

    NWB_ASSERT(meshRenderBindingsReady(mesh));
    if(!meshRenderBindingsReady(mesh))
        return false;

    __hidden_mesh::RefreshRuntimeMeshContent(mesh, desc);
    outMesh = &mesh;
    NWB_ASSERT(outMesh->valid());
    return true;
}

void RendererMeshSystem::pruneRuntimeMeshResources(){
    if(m_meshState.m_meshes.empty())
        return;

    const auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    Core::Alloc::ScratchArena scratchArena(__hidden_mesh::s_RuntimeMeshPruningArena);
    if(meshSystemPtr){
        const NWB::Impl::MeshSystem& meshSystem = *meshSystemPtr;
        ECSRenderDetail::PruneRuntimeMeshResources(
            m_meshState.m_meshes,
            meshSystem,
            [this](MeshResources& mesh){ releaseMeshGeometryHeapHandles(mesh); },
            scratchArena
        );
        return;
    }
    ECSRenderDetail::PruneRuntimeMeshResources(
        m_meshState.m_meshes,
        meshSystemPtr,
        [this](MeshResources& mesh){ releaseMeshGeometryHeapHandles(mesh); },
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

