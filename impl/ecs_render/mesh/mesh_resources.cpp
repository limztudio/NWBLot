// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"
#include "runtime_mesh_pruning.h"

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_triangle_indices.h>
#include <impl/assets_mesh/meshlet_vertex_attributes.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>
#include <impl/ecs_mesh/runtime/resource_names.h>

#include <core/alloc/scratch.h>
#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_RuntimeMeshPruningArena("impl/ecs_render/runtime_mesh_pruning");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UnpublishedMeshDescriptors final : NoCopy{
public:
    UnpublishedMeshDescriptors(RendererMeshSystem& meshSystem, MeshResources& mesh)noexcept
        : m_meshSystem(meshSystem)
        , m_mesh(mesh)
    {}
    ~UnpublishedMeshDescriptors()noexcept(noexcept(DeclVal<RendererMeshSystem&>().releaseMeshGeometryHeapHandles(DeclVal<MeshResources&>()))){
        if(m_active)
            m_meshSystem.releaseMeshGeometryHeapHandles(m_mesh);
    }


public:
    void release()noexcept{ m_active = false; }


private:
    RendererMeshSystem& m_meshSystem;
    MeshResources& m_mesh;
    bool m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool RuntimeMeshSourceMatches(const MeshResources& mesh, const RuntimeMeshDesc& desc)noexcept{
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
        && mesh.runtimeLocalBoundsBuffer == desc.localBoundsBuffer
        && mesh.runtimeMeshletLocalBoundsBuffer == desc.meshletLocalBoundsBuffer
        && mesh.meshletCount == desc.meshletCount
        && mesh.meshletPrimitiveIndexCount == desc.meshletPrimitiveIndexCount
    ;
}

static void RefreshRuntimeMeshContent(MeshResources& mesh, const RuntimeMeshDesc& desc)noexcept{
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

static void ReportMeshBufferSetupFailure(const RuntimeMeshBufferUpload::BufferSetupFailure::Enum failure, const Name& meshName, const TStringView label){
    switch(failure){
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has empty {} payload"), StringConvert(meshName.resolvedText()), label);
        return;
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' {} payload byte size overflows"), StringConvert(meshName.resolvedText()), label);
        return;
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        break;
    }
    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create {} buffer for mesh '{}'"), label, StringConvert(meshName.resolvedText()));
    return;
}

[[nodiscard]] static Expected<Name> DeriveMeshBufferName(const Name& meshName, const AStringView suffix, const TStringView label){
    const auto bufferName = DeriveName(meshName, suffix);
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive {} buffer name for mesh '{}'"), label, StringConvert(meshName.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    return bufferName;
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] static Expected<Core::BufferHandle, RuntimeMeshBufferUpload::BufferSetupFailure::Enum> SetupMeshBuffer(
    Core::GraphicsRuntime& graphics,
    const Name& meshName,
    const AStringView suffix,
    const PayloadVector& payload,
    const TStringView label,
    const bool canHaveRawViews = false,
    const bool accelStructBuildInput = false
){
    const auto bufferName = DeriveMeshBufferName(meshName, suffix, label);
    if(!bufferName)
        return MakeUnexpected(RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed);

    auto buffer = RuntimeMeshBufferUpload::SetupRequiredBuffer<PayloadT>(graphics, *bufferName, payload, { false, canHaveRawViews, accelStructBuildInput, Core::ResourceQueueSharing::GraphicsAndAsyncCompute });
    if(!buffer){
        ReportMeshBufferSetupFailure(buffer.error(), meshName, label);
        return MakeUnexpected(buffer.error());
    }
    return Move(*buffer);
}

template<typename PayloadVector>
[[nodiscard]] static Expected<Core::BufferHandle, RuntimeMeshBufferUpload::BufferSetupFailure::Enum> SetupPaddedRawMeshBuffer(
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& meshName,
    const AStringView suffix,
    const PayloadVector& payload,
    const TStringView label
){
    const auto bufferName = DeriveMeshBufferName(meshName, suffix, label);
    if(!bufferName)
        return MakeUnexpected(RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed);

    auto buffer = RuntimeMeshBufferUpload::SetupRequiredPaddedRawByteBuffer(graphics, scratchArena, *bufferName, payload, { false, true, false, Core::ResourceQueueSharing::GraphicsAndAsyncCompute });
    if(!buffer){
        ReportMeshBufferSetupFailure(buffer.error(), meshName, label);
        return MakeUnexpected(buffer.error());
    }
    return Move(*buffer);
}

[[nodiscard]] static bool ValidateRawBufferLogicalByteCount(const Core::BufferHandle& buffer, const u32 logicalByteCount, const Name& meshName, const TStringView label){
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has no {} buffer"), StringConvert(meshName.resolvedText()), label);
        return false;
    }

    const Core::BufferDesc& desc = buffer->getCreationDescription();
    if(desc.structStride != sizeof(u8) || desc.byteSize < static_cast<u64>(logicalByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' {} buffer cannot cover {} logical bytes"), StringConvert(meshName.resolvedText()), label, logicalByteCount);
        return false;
    }
    return true;
}

template<typename PositionVector>
[[nodiscard]] static Expected<CsgReceiverCpuBounds> BuildPositionStreamBounds(const PositionVector& positions)noexcept([]()constexpr noexcept{
    using Range = PositionVector;
    if constexpr(IsArray_V<Range>)
        return noexcept(DeclVal<void(*)(const Float3U&)noexcept>()(DeclVal<const RemoveExtent_T<Range>&>()));
    else if constexpr(requires(const Range& values){ values.begin(); values.end(); }){
        using Iterator = RemoveCVRef_T<decltype(DeclVal<const Range&>().begin())>;
        using Sentinel = RemoveCVRef_T<decltype(DeclVal<const Range&>().end())>;
        return
            noexcept(DeclVal<void(*)(Iterator)noexcept>()(DeclVal<const Range&>().begin()))
            && noexcept(DeclVal<void(*)(Sentinel)noexcept>()(DeclVal<const Range&>().end()))
            && IsNothrowDestructible_V<Iterator>
            && IsNothrowDestructible_V<Sentinel>
            && noexcept(static_cast<bool>(DeclVal<Iterator&>() != DeclVal<Sentinel&>()))
            && noexcept(++DeclVal<Iterator&>())
            && noexcept(DeclVal<void(*)(const Float3U&)noexcept>()(*DeclVal<Iterator&>()))
        ;
    }
    else
        return false;
}() && noexcept(static_cast<bool>(positions.empty()))){
    CsgReceiverCpuBounds bounds;
    if(positions.empty())
        return MakeUnexpected(Failure{});

    SIMDVectorSingle minBounds;
    SIMDVectorSingle maxBounds;
    AabbTests::Reset(minBounds, maxBounds);
    for(const Float3U& position : positions)
        AabbTests::Expand(LoadFloat(position), minBounds, maxBounds);

    if(!AabbTests::Valid(minBounds, maxBounds))
        return MakeUnexpected(Failure{});

    StoreFloatInt(VectorSetW(minBounds, 0.0f), s_CsgBoundsValidFlag | s_CsgBoundsFiniteFlag, bounds.minBounds);
    StoreFloatInt(VectorSetW(maxBounds, 0.0f), 0, bounds.maxBounds);
    return bounds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MeshResources*> RendererMeshSystem::createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, Core::Alloc::ScratchArena& scratchArena){

    const Name meshPath = meshAsset.name();
    if(!meshPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer mesh is empty"));
        return MakeUnexpected(Failure{});
    }

    const auto foundMesh = m_meshState.m_meshes.find(meshPath);
    if(foundMesh != m_meshState.m_meshes.end()){
        NWB_ASSERT(meshRenderBindingsReady(foundMesh.value()));
        NWB_ASSERT(foundMesh.value().valid());
        return &foundMesh.value();
    }

    auto loadedAsset = m_assetManager.loadTypedSync<Mesh>(meshPath, NWB_TEXT("RendererSystem"), Mesh::s_AssetTypeText);
    if(!loadedAsset)
        return MakeUnexpected(Failure{});

    const Mesh& mesh = **loadedAsset;
    // Mesh::loadBinary already ran the full payload validation (including u32 stream limits).
    NWB_ASSERT(mesh.validatePayload(scratchArena));
    NWB_ASSERT(mesh.meshlets().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(mesh.meshletPrimitiveIndices().size() <= static_cast<usize>(Limit<u32>::s_Max));

    MeshResources createdMesh;
    createdMesh.meshName = meshPath;
    createdMesh.meshletCount = static_cast<u32>(mesh.meshlets().size());
    createdMesh.meshletPrimitiveIndexCount = static_cast<u32>(mesh.meshletPrimitiveIndices().size());
    createdMesh.solidTriangleWords = Core::MakeGlobalUnique<u32[]>(m_arena, mesh.solidTriangleWords().size());
    if(!createdMesh.solidTriangleWords)
        return MakeUnexpected(Failure{});
    const usize solidByteCount = mesh.solidTriangleWords().size() * sizeof(u32);
    NWB_MEMCPY(createdMesh.solidTriangleWords.get(), solidByteCount, mesh.solidTriangleWords().data(), solidByteCount);
    const auto positionBounds = __hidden_mesh::BuildPositionStreamBounds(mesh.positionStream());
    if(!positionBounds){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh '{}' has invalid CSG receiver bounds"), StringConvert(meshPath.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    createdMesh.csgLocalBounds = *positionBounds;

    const bool rtSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct);
    // Both tracing backends read raw position/index buffers for material evaluation.
    const bool swShadow = !rtSupported;

    bool uploaded = true;
    auto positionBufferResult = __hidden_mesh::SetupMeshBuffer<Float3U>(m_graphics, meshPath, RendererArenaScope::s_PositionsBufferName, mesh.positionStream(), RendererArenaScope::s_PositionBufferLabel, true, rtSupported);
    if(positionBufferResult)
        createdMesh.positionBuffer = Move(*positionBufferResult);
    else
        createdMesh.positionBuffer.reset();
    uploaded = static_cast<bool>(positionBufferResult) && uploaded;
    auto normalBufferResult = __hidden_mesh::SetupMeshBuffer<Half4U>(m_graphics, meshPath, RendererArenaScope::s_NormalsBufferName, mesh.normalStream(), RendererArenaScope::s_NormalBufferLabel);
    if(normalBufferResult)
        createdMesh.normalBuffer = Move(*normalBufferResult);
    else
        createdMesh.normalBuffer.reset();
    uploaded = static_cast<bool>(normalBufferResult) && uploaded;
    auto tangentBufferResult = __hidden_mesh::SetupMeshBuffer<Half4U>(m_graphics, meshPath, RendererArenaScope::s_TangentsBufferName, mesh.tangentStream(), RendererArenaScope::s_TangentBufferLabel);
    if(tangentBufferResult)
        createdMesh.tangentBuffer = Move(*tangentBufferResult);
    else
        createdMesh.tangentBuffer.reset();
    uploaded = static_cast<bool>(tangentBufferResult) && uploaded;
    auto uv0BufferResult = __hidden_mesh::SetupMeshBuffer<Float2U>(m_graphics, meshPath, RendererArenaScope::s_Uv0BufferName, mesh.uv0Stream(), RendererArenaScope::s_Uv0BufferLabel);
    if(uv0BufferResult)
        createdMesh.uv0Buffer = Move(*uv0BufferResult);
    else
        createdMesh.uv0Buffer.reset();
    uploaded = static_cast<bool>(uv0BufferResult) && uploaded;
    auto colorBufferResult = __hidden_mesh::SetupMeshBuffer<Half4U>(m_graphics, meshPath, RendererArenaScope::s_ColorsBufferName, mesh.colorStream(), RendererArenaScope::s_ColorBufferLabel);
    if(colorBufferResult)
        createdMesh.colorBuffer = Move(*colorBufferResult);
    else
        createdMesh.colorBuffer.reset();
    uploaded = static_cast<bool>(colorBufferResult) && uploaded;
    auto meshletDescBufferResult = __hidden_mesh::SetupMeshBuffer<MeshletDesc>(m_graphics, meshPath, RendererArenaScope::s_MeshletsBufferName, mesh.meshlets(), RendererArenaScope::s_MeshletDescriptorBufferLabel);
    if(meshletDescBufferResult)
        createdMesh.meshletDescBuffer = Move(*meshletDescBufferResult);
    else
        createdMesh.meshletDescBuffer.reset();
    uploaded = static_cast<bool>(meshletDescBufferResult) && uploaded;
    auto meshletBoundsBufferResult = __hidden_mesh::SetupMeshBuffer<MeshletBounds>(m_graphics, meshPath, RendererArenaScope::s_MeshletBoundsBufferName, mesh.meshletBounds(), RendererArenaScope::s_MeshletBoundsBufferLabel, true);
    if(meshletBoundsBufferResult)
        createdMesh.meshletBoundsBuffer = Move(*meshletBoundsBufferResult);
    else
        createdMesh.meshletBoundsBuffer.reset();
    uploaded = static_cast<bool>(meshletBoundsBufferResult) && uploaded;
    auto meshletPositionRefDeltaBufferResult = __hidden_mesh::SetupPaddedRawMeshBuffer(m_graphics, scratchArena, meshPath, RendererArenaScope::s_MeshletPositionRefDeltasBufferName, mesh.meshletPositionRefDeltas(), RendererArenaScope::s_MeshletPositionRefDeltaBufferLabel);
    if(meshletPositionRefDeltaBufferResult)
        createdMesh.meshletPositionRefDeltaBuffer = Move(*meshletPositionRefDeltaBufferResult);
    else
        createdMesh.meshletPositionRefDeltaBuffer.reset();
    uploaded = static_cast<bool>(meshletPositionRefDeltaBufferResult) && uploaded;
    auto meshletAttributeRefDeltaBufferResult = __hidden_mesh::SetupPaddedRawMeshBuffer(m_graphics, scratchArena, meshPath, RendererArenaScope::s_MeshletAttributeRefDeltasBufferName, mesh.meshletAttributeRefDeltas(), RendererArenaScope::s_MeshletAttributeRefDeltaBufferLabel);
    if(meshletAttributeRefDeltaBufferResult)
        createdMesh.meshletAttributeRefDeltaBuffer = Move(*meshletAttributeRefDeltaBufferResult);
    else
        createdMesh.meshletAttributeRefDeltaBuffer.reset();
    uploaded = static_cast<bool>(meshletAttributeRefDeltaBufferResult) && uploaded;
    auto meshletLocalVertexRefBufferResult = __hidden_mesh::SetupMeshBuffer<MeshletLocalVertexRef>(m_graphics, meshPath, RendererArenaScope::s_MeshletLocalVertexRefsBufferName, mesh.meshletLocalVertexRefs(), RendererArenaScope::s_MeshletLocalVertexRefBufferLabel);
    if(meshletLocalVertexRefBufferResult)
        createdMesh.meshletLocalVertexRefBuffer = Move(*meshletLocalVertexRefBufferResult);
    else
        createdMesh.meshletLocalVertexRefBuffer.reset();
    uploaded = static_cast<bool>(meshletLocalVertexRefBufferResult) && uploaded;
    auto meshletPrimitiveIndexBufferResult = __hidden_mesh::SetupPaddedRawMeshBuffer(m_graphics, scratchArena, meshPath, RendererArenaScope::s_MeshletPrimitiveIndicesBufferName, mesh.meshletPrimitiveIndices(), RendererArenaScope::s_MeshletPrimitiveIndexBufferLabel);
    if(meshletPrimitiveIndexBufferResult)
        createdMesh.meshletPrimitiveIndexBuffer = Move(*meshletPrimitiveIndexBufferResult);
    else
        createdMesh.meshletPrimitiveIndexBuffer.reset();
    uploaded = static_cast<bool>(meshletPrimitiveIndexBufferResult) && uploaded;
    if(!uploaded)
        return MakeUnexpected(Failure{});

    // Both shadow backends trace triangles; always create the reconstructed index buffer.
    {
        const auto triangleIndices = BuildMeshletTriangleIndices(scratchArena, mesh);
        if(!triangleIndices){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to reconstruct shadow trace triangle indices for mesh '{}'"), StringConvert(meshPath.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        const auto indexBufferName = DeriveName(meshPath, MeshResourceNames::s_RtTriangleIndicesBufferName);
        if(!indexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive shadow trace index buffer name for mesh '{}'"), StringConvert(meshPath.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        RuntimeMeshBufferUpload::BufferFlags indexFlags;
        indexFlags.canHaveRawViews = true;
        indexFlags.isIndexBuffer = true;
        indexFlags.accelStructBuildInput = rtSupported;
        // Async shadow packet shares this stream; keep sharing consistent to avoid ownership transfer.
        indexFlags.queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute;
        auto indexBuffer = RuntimeMeshBufferUpload::SetupRequiredBuffer<u32>(m_graphics, *indexBufferName, *triangleIndices, indexFlags);
        if(!indexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow trace index buffer for mesh '{}'"), StringConvert(meshPath.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        createdMesh.triangleIndexBuffer = Move(*indexBuffer);

        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: mesh '{}' shadow trace index buffer ready ({} indices, expected {})")
            , StringConvert(meshPath.resolvedText())
            , static_cast<u64>(triangleIndices->size())
            , static_cast<u64>(createdMesh.meshletPrimitiveIndexCount)
        );

        createdMesh.blasBuildPending = rtSupported;
        // Keep software geometry pending until a software preparation route accepts its build.
        createdMesh.swBvhBuildPending = swShadow || rtSupported;
    }

    // Flat per-corner trace attributes in lockstep with the index buffer; always carries a raw view.
    {
        const auto triangleAttributes = BuildMeshletTriangleAttributes(scratchArena, mesh);
        if(!triangleAttributes){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to reconstruct shadow trace triangle attributes for mesh '{}'"), StringConvert(meshPath.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        const auto attributeBufferName = DeriveName(meshPath, MeshResourceNames::s_RtTriangleAttributesBufferName);
        if(!attributeBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive shadow trace attribute buffer name for mesh '{}'"), StringConvert(meshPath.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        RuntimeMeshBufferUpload::BufferFlags attributeFlags;
        attributeFlags.canHaveRawViews = true;
        // Tracing shares these attributes on AsyncCompute; keep shared-read input.
        attributeFlags.queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute;
        auto attributeBuffer = RuntimeMeshBufferUpload::SetupRequiredBuffer<AttribGpu>(m_graphics, *attributeBufferName, *triangleAttributes, attributeFlags);
        if(!attributeBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow trace triangle attribute buffer for mesh '{}'"), StringConvert(meshPath.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        createdMesh.attributeBuffer = Move(*attributeBuffer);

    }

    NWB_ASSERT(createdMesh.valid());
    __hidden_mesh::UnpublishedMeshDescriptors retireUnpublished(*this, createdMesh);
    if(!createMeshRenderBindings(createdMesh))
        return MakeUnexpected(Failure{});

    auto result = m_meshState.m_meshes.try_emplace(meshPath, Move(createdMesh));
    if(result.second)
        retireUnpublished.release();
    auto it = result.first;

    NWB_ASSERT(it.value().valid());
    return &it.value();
}

Expected<MeshResources*> RendererMeshSystem::findMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset)noexcept{

    const Name meshPath = meshAsset.name();
    return findMeshResources(meshPath);
}

Expected<MeshResources*> RendererMeshSystem::findMeshResources(const Name& meshKey)noexcept{
    if(!meshKey)
        return MakeUnexpected(Failure{});

    const auto foundMesh = m_meshState.m_meshes.find(meshKey);
    if(foundMesh == m_meshState.m_meshes.end())
        return MakeUnexpected(Failure{});

    NWB_ASSERT(meshRenderBindingsReady(foundMesh.value()));
    NWB_ASSERT(foundMesh.value().valid());
    return &foundMesh.value();
}

Expected<MeshResources*> RendererMeshSystem::createRuntimeMeshResources(const RuntimeMeshDesc& desc){

    NWB_ASSERT(desc.valid());

    const auto foundMesh = m_meshState.m_meshes.find(desc.meshKey);
    if(foundMesh != m_meshState.m_meshes.end()){
        if(!foundMesh.value().runtimeMesh){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime mesh '{}' collides with a static mesh resource"), StringConvert(desc.meshKey.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        if(!__hidden_mesh::RuntimeMeshSourceMatches(foundMesh.value(), desc)){
            releaseMeshGeometryHeapHandles(foundMesh.value());
            m_meshState.m_meshes.erase(foundMesh);
        }
        else{
            NWB_ASSERT(meshRenderBindingsReady(foundMesh.value()));
            MeshResources& mesh = foundMesh.value();
            __hidden_mesh::RefreshRuntimeMeshContent(mesh, desc);
            NWB_ASSERT(mesh.valid());
            return &mesh;
        }
    }

    MeshResources createdMesh;
    createdMesh.meshName = desc.meshKey;
    createdMesh.solidTriangleWords = Core::MakeGlobalUnique<u32[]>(m_arena, desc.solidTriangleWords.size());
    if(!createdMesh.solidTriangleWords)
        return MakeUnexpected(Failure{});
    const usize solidByteCount = desc.solidTriangleWords.size_bytes();
    NWB_MEMCPY(createdMesh.solidTriangleWords.get(), solidByteCount, desc.solidTriangleWords.data(), solidByteCount);
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
    createdMesh.runtimeLocalBoundsBuffer = desc.localBoundsBuffer;
    createdMesh.runtimeMeshletLocalBoundsBuffer = desc.meshletLocalBoundsBuffer;
    createdMesh.blasBuildPending = (desc.triangleIndexBuffer != nullptr);
    createdMesh.meshletCount = desc.meshletCount;
    createdMesh.meshletPrimitiveIndexCount = desc.meshletPrimitiveIndexCount;
    createdMesh.runtimeMesh = true;
    createdMesh.runtimeMeshVersion = desc.version;
    NWB_ASSERT(desc.localBounds.valid());
    __hidden_mesh::RefreshRuntimeMeshContent(createdMesh, desc);
    if((createdMesh.meshletPrimitiveIndexCount % s_MeshletTriangleIndexCount) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime mesh '{}' has {} primitive-index bytes, which cannot form triangles"), StringConvert(createdMesh.meshName.resolvedText()), createdMesh.meshletPrimitiveIndexCount);
        return MakeUnexpected(Failure{});
    }
    if(!__hidden_mesh::ValidateRawBufferLogicalByteCount(createdMesh.meshletPrimitiveIndexBuffer, createdMesh.meshletPrimitiveIndexCount, createdMesh.meshName, RendererArenaScope::s_MeshletPrimitiveIndexBufferLabel))
        return MakeUnexpected(Failure{});
    NWB_ASSERT(createdMesh.valid());
    __hidden_mesh::UnpublishedMeshDescriptors retireUnpublished(*this, createdMesh);
    if(!createMeshRenderBindings(createdMesh))
        return MakeUnexpected(Failure{});

    auto result = m_meshState.m_meshes.try_emplace(desc.meshKey, Move(createdMesh));
    if(result.second)
        retireUnpublished.release();
    auto it = result.first;

    NWB_ASSERT(it.value().valid());
    return &it.value();
}

Expected<MeshResources*> RendererMeshSystem::findRuntimeMeshResources(const RuntimeMeshDesc& desc)noexcept{
    NWB_ASSERT(desc.valid());

    const auto foundMesh = m_meshState.m_meshes.find(desc.meshKey);
    if(foundMesh == m_meshState.m_meshes.end())
        return MakeUnexpected(Failure{});

    MeshResources& mesh = foundMesh.value();
    if(!mesh.runtimeMesh || !__hidden_mesh::RuntimeMeshSourceMatches(mesh, desc))
        return MakeUnexpected(Failure{});

    NWB_ASSERT(meshRenderBindingsReady(mesh));
    __hidden_mesh::RefreshRuntimeMeshContent(mesh, desc);
    NWB_ASSERT(mesh.valid());
    return &mesh;
}

void RendererMeshSystem::pruneRuntimeMeshResources(){
    if(m_meshState.m_meshes.empty())
        return;

    const auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    Core::Alloc::ScratchArena scratchArena(__hidden_mesh::s_RuntimeMeshPruningArena);
    if(meshSystemPtr){
        const NWB::Impl::MeshSystem& meshSystem = *meshSystemPtr;
        ECSRenderDetail::PruneRuntimeMeshResources(m_meshState.m_meshes, meshSystem, [this](MeshResources& mesh){ releaseMeshGeometryHeapHandles(mesh); }, scratchArena);
        return;
    }
    ECSRenderDetail::PruneRuntimeMeshResources(m_meshState.m_meshes, meshSystemPtr, [this](MeshResources& mesh){ releaseMeshGeometryHeapHandles(mesh); }, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

