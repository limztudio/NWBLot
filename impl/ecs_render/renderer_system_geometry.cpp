// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system_private.h"

#include <bit>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_system_geometry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct StaticGeometrySourceVertexWords{
    u32 positionX = 0u;
    u32 positionY = 0u;
    u32 positionZ = 0u;
    u32 normalXy = 0u;
    u32 normalZw = 0u;
    u32 colorXy = 0u;
    u32 colorZw = 0u;
};
static_assert(
    sizeof(StaticGeometrySourceVertexWords) == sizeof(u32) * ECSRenderDetail::s_StaticGeometrySourceWordStride,
    "Static geometry source word layout drifted"
);

using SourceVertexWordVector = Vector<StaticGeometrySourceVertexWords, Core::Alloc::ScratchArena<>>;

[[nodiscard]] static u32 FloatWord(const f32 value){
    return std::bit_cast<u32>(value);
}

[[nodiscard]] static bool BuildStaticGeometrySourceWords(const Geometry& geometry, SourceVertexWordVector& outWords, usize& outBytes){
    outWords.clear();
    outBytes = 0u;

    const usize vertexCount = geometry.vertexCount();
    if(vertexCount == 0u)
        return false;
    if(vertexCount > Limit<usize>::s_Max / sizeof(StaticGeometrySourceVertexWords))
        return false;

    const usize sourceVertexBytes = vertexCount * sizeof(StaticGeometrySourceVertexWords);

    const auto& positions = geometry.positions();
    const auto& normals = geometry.normals();
    const auto& colors = geometry.colors();
    outWords.reserve(vertexCount);
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        const Float3U& position = positions[vertexIndex];
        const Half4U& normal = normals[vertexIndex];
        const Half4U& color = colors[vertexIndex];

        StaticGeometrySourceVertexWords vertexWords;
        vertexWords.positionX = FloatWord(position.x);
        vertexWords.positionY = FloatWord(position.y);
        vertexWords.positionZ = FloatWord(position.z);
        vertexWords.normalXy = normal.packed[0];
        vertexWords.normalZw = normal.packed[1];
        vertexWords.colorXy = color.packed[0];
        vertexWords.colorZw = color.packed[1];
        outWords.push_back(vertexWords);
    }
    outBytes = sourceVertexBytes;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererSystem::destroyGeometryBindingSets(){
    m_emulationViewBindingSet = nullptr;
    for(auto it = m_geometryMeshes.begin(); it != m_geometryMeshes.end(); ++it){
        GeometryResources& geometry = it.value();
        geometry.meshBindingSet = nullptr;
        geometry.computeBindingSet = nullptr;
    }
}

bool RendererSystem::createGeometryResources(const Core::Assets::AssetRef<Geometry>& geometryAsset, GeometryResources*& outGeometry){
    outGeometry = nullptr;

    const Name geometryPath = geometryAsset.name();
    if(!geometryPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer geometry is empty"));
        return false;
    }

    const auto foundGeometry = m_geometryMeshes.find(geometryPath);
    if(foundGeometry != m_geometryMeshes.end()){
        outGeometry = &foundGeometry.value();
        return outGeometry->valid();
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Geometry::AssetTypeName(), geometryPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to load geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Geometry::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: asset '{}' is not geometry"), StringConvert(geometryPath.c_str()));
        return false;
    }

    const Geometry& geometry = static_cast<const Geometry&>(*loadedAsset);

    GeometryResources createdGeometry;
    createdGeometry.geometryName = geometryPath;
    createdGeometry.sourceVertexLayout = MeshSourceLayout::StaticGeometryStreams;

    const usize indexCount = geometry.indices().size();
    if(indexCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' index count exceeds u32 limits"), StringConvert(geometryPath.c_str()));
        return false;
    }

    createdGeometry.indexCount = static_cast<u32>(indexCount);
    if(createdGeometry.indexCount == 0 || (createdGeometry.indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' index count {} is incompatible with triangle-based mesh rendering")
            , StringConvert(geometryPath.c_str())
            , createdGeometry.indexCount
        );
        return false;
    }

    createdGeometry.triangleCount = createdGeometry.indexCount / 3u;
    createdGeometry.dispatchGroupCount = DivideUp(createdGeometry.triangleCount, ECSRenderDetail::s_TrianglesPerWorkgroup);
    if(createdGeometry.dispatchGroupCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' produced no dispatch groups"), StringConvert(geometryPath.c_str()));
        return false;
    }

    const Name shaderVertexBufferName = DeriveName(geometryPath, AStringView(":shader_vb"));
    const Name shaderIndexBufferName = DeriveName(geometryPath, AStringView(":shader_ib"));
    if(!shaderVertexBufferName || !shaderIndexBufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive shader-driven buffer names for geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }

    Core::Alloc::ScratchArena<> scratchArena;
    __hidden_renderer_system_geometry::SourceVertexWordVector sourceVertexWords{ scratchArena };
    usize sourceVertexBytes = 0u;
    if(!__hidden_renderer_system_geometry::BuildStaticGeometrySourceWords(geometry, sourceVertexWords, sourceVertexBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' source vertex stream size overflows"), StringConvert(geometryPath.c_str()));
        return false;
    }

    Core::Graphics::BufferSetupDesc shaderVertexSetup;
    shaderVertexSetup.bufferDesc
        .setByteSize(static_cast<u64>(sourceVertexBytes))
        .setStructStride(sizeof(u32))
        .setDebugName(shaderVertexBufferName)
    ;
    shaderVertexSetup.data = sourceVertexWords.data();
    shaderVertexSetup.dataSize = sourceVertexBytes;
    createdGeometry.shaderVertexBuffer = m_graphics.setupBuffer(shaderVertexSetup);
    if(!createdGeometry.shaderVertexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shader vertex buffer for geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }

    const usize expandedIndexCount = static_cast<usize>(createdGeometry.indexCount);
    if(expandedIndexCount > Limit<usize>::s_Max / sizeof(u32)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' expanded index buffer size overflows"), StringConvert(geometryPath.c_str()));
        return false;
    }
    const usize expandedIndexBytes = expandedIndexCount * sizeof(u32);

    Core::Graphics::BufferSetupDesc shaderIndexSetup;
    shaderIndexSetup.bufferDesc
        .setByteSize(static_cast<u64>(expandedIndexBytes))
        .setStructStride(sizeof(u32))
        .setDebugName(shaderIndexBufferName)
    ;
    shaderIndexSetup.data = geometry.indices().data();
    shaderIndexSetup.dataSize = expandedIndexBytes;
    createdGeometry.shaderIndexBuffer = m_graphics.setupBuffer(shaderIndexSetup);
    if(!createdGeometry.shaderIndexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shader index buffer for geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }

    auto result = m_geometryMeshes.try_emplace(geometryPath, Move(createdGeometry));
    auto it = result.first;

    outGeometry = &it.value();
    return outGeometry->valid();
}

bool RendererSystem::createRuntimeGeometryResources(const RuntimeGeometryDesc& desc, GeometryResources*& outGeometry){
    outGeometry = nullptr;

    if(!desc.valid())
        return false;

    const auto foundGeometry = m_geometryMeshes.find(desc.geometryKey);
    if(foundGeometry != m_geometryMeshes.end()){
        if(!foundGeometry.value().runtimeGeometry){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime geometry '{}' collides with a static geometry resource")
                , StringConvert(desc.geometryKey.c_str())
            );
            return false;
        }
        if(foundGeometry.value().runtimeGeometryVersion != desc.version){
            m_geometryMeshes.erase(foundGeometry);
        }
        else{
            outGeometry = &foundGeometry.value();
            return outGeometry->valid();
        }
    }

    GeometryResources createdGeometry;
    createdGeometry.geometryName = desc.geometryKey;
    createdGeometry.shaderVertexBuffer = desc.shaderVertexBuffer;
    createdGeometry.shaderIndexBuffer = desc.shaderIndexBuffer;
    createdGeometry.indexCount = desc.indexCount;
    createdGeometry.triangleCount = createdGeometry.indexCount / 3u;
    createdGeometry.dispatchGroupCount = DivideUp(createdGeometry.triangleCount, ECSRenderDetail::s_TrianglesPerWorkgroup);
    createdGeometry.sourceVertexLayout = desc.sourceVertexLayout;
    createdGeometry.runtimeGeometry = true;
    createdGeometry.runtimeGeometryVersion = desc.version;
    if(createdGeometry.dispatchGroupCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime geometry '{}' produced no dispatch groups"), StringConvert(desc.geometryKey.c_str()));
        return false;
    }

    auto result = m_geometryMeshes.try_emplace(desc.geometryKey, Move(createdGeometry));
    auto it = result.first;

    outGeometry = &it.value();
    return outGeometry->valid();
}

void RendererSystem::pruneRuntimeGeometryResources(){
    if(m_geometryMeshes.empty())
        return;

    const auto* geometrySystem = m_world.getSystem<NWB::Impl::GeometrySystem>();
    for(auto it = m_geometryMeshes.begin(); it != m_geometryMeshes.end();){
        const GeometryResources& geometry = it.value();
        if(!geometry.runtimeGeometry){
            ++it;
            continue;
        }

        if(geometrySystem && geometrySystem->containsRuntimeGeometry(geometry.geometryName, geometry.runtimeGeometryVersion)){
            ++it;
            continue;
        }

        it = m_geometryMeshes.erase(it);
    }
}

bool RendererSystem::createMeshBindingSet(GeometryResources& geometry){
    if(geometry.meshBindingSet)
        return true;
    if(!createMeshShaderResources())
        return false;
    if(!m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh binding set requires an instance buffer"));
        return false;
    }
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh binding set requires a mesh view buffer"));
        return false;
    }
    if(!m_materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh binding set requires a material typed buffer"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc(m_arena);
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, geometry.shaderVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, geometry.shaderIndexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(6, m_materialTypedBuffer.get()));

    Core::IDevice* device = m_graphics.getDevice();
    geometry.meshBindingSet = device->createBindingSet(bindingSetDesc, m_meshBindingLayout);
    if(!geometry.meshBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding set for geometry '{}'"), StringConvert(geometry.geometryName.c_str()));
        return false;
    }

    return true;
}

bool RendererSystem::createComputeBindingSet(GeometryResources& geometry){
    if(geometry.computeBindingSet)
        return true;
    if(!createComputeEmulationResources())
        return false;
    if(!m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute binding set requires an instance buffer"));
        return false;
    }
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute binding set requires a mesh view buffer"));
        return false;
    }
    if(!m_materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute binding set requires a material typed buffer"));
        return false;
    }

    if(!geometry.emulationVertexBuffer){
        const Name emulationVertexBufferName = DeriveName(geometry.geometryName, AStringView(":emulation_vb"));
        if(!emulationVertexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive compute-emulation vertex buffer name for geometry '{}'")
                , StringConvert(geometry.geometryName.c_str())
            );
            return false;
        }

        Core::BufferDesc emulationVertexBufferDesc;
        emulationVertexBufferDesc
            .setByteSize(static_cast<u64>(geometry.indexCount) * ECSRenderDetail::s_EmulatedVertexStride)
            .setStructStride(ECSRenderDetail::s_EmulatedVertexStride)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setDebugName(emulationVertexBufferName)
        ;
        geometry.emulationVertexBuffer = m_graphics.createBuffer(emulationVertexBufferDesc);
        if(!geometry.emulationVertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for geometry '{}'")
                , StringConvert(geometry.geometryName.c_str())
            );
            return false;
        }
    }

    Core::BindingSetDesc bindingSetDesc(m_arena);
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, geometry.shaderVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, geometry.shaderIndexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, geometry.emulationVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(6, m_materialTypedBuffer.get()));

    Core::IDevice* device = m_graphics.getDevice();
    geometry.computeBindingSet = device->createBindingSet(bindingSetDesc, m_computeBindingLayout);
    if(!geometry.computeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding set for geometry '{}'")
            , StringConvert(geometry.geometryName.c_str())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

