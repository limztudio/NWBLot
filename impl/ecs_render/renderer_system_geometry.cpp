// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system_private.h"

#include <impl/ecs_geometry/runtime_geometry_buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_system_geometry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PayloadT, typename PayloadVector>
[[nodiscard]] static Core::BufferHandle SetupGeometryBuffer(
    Core::Graphics& graphics,
    const Name& geometryName,
    const AStringView suffix,
    const PayloadVector& payload,
    const tchar* label,
    const bool canHaveRawViews = false
){
    if(payload.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' has empty {} payload")
            , StringConvert(geometryName.c_str())
            , label
        );
        return {};
    }
    if(!RuntimeGeometryBufferUpload::PayloadByteCountFits<PayloadT>(payload)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' {} payload byte size overflows")
            , StringConvert(geometryName.c_str())
            , label
        );
        return {};
    }

    const Name bufferName = DeriveName(geometryName, suffix);
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive {} buffer name for geometry '{}'")
            , label
            , StringConvert(geometryName.c_str())
        );
        return {};
    }

    Core::BufferHandle buffer = RuntimeGeometryBufferUpload::SetupBuffer<PayloadT>(
        graphics,
        bufferName,
        payload,
        { false, canHaveRawViews }
    );
    if(buffer)
        return buffer;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create {} buffer for geometry '{}'")
        , label
        , StringConvert(geometryName.c_str())
    );
    return {};
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] static bool AssignGeometryBuffer(
    Core::Graphics& graphics,
    const Name& geometryName,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const tchar* label,
    const bool canHaveRawViews = false
){
    outBuffer = SetupGeometryBuffer<PayloadT>(
        graphics,
        geometryName,
        suffix,
        payload,
        label,
        canHaveRawViews
    );
    return outBuffer != nullptr;
}

[[nodiscard]] static bool ResolveBufferElementCount(
    const Core::BufferHandle& buffer,
    u32& outCount,
    const Name& geometryName,
    const tchar* label
){
    outCount = 0u;
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' has no {} buffer")
            , StringConvert(geometryName.c_str())
            , label
        );
        return false;
    }

    const Core::BufferDesc& desc = buffer->getDescription();
    if(desc.structStride == 0u || desc.byteSize == 0u || (desc.byteSize % desc.structStride) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' {} buffer has invalid structured layout")
            , StringConvert(geometryName.c_str())
            , label
        );
        return false;
    }

    const u64 count = desc.byteSize / desc.structStride;
    if(count > static_cast<u64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' {} buffer element count exceeds u32 limits")
            , StringConvert(geometryName.c_str())
            , label
        );
        return false;
    }

    outCount = static_cast<u32>(count);
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
    if(!geometry.validatePayload())
        return false;

    if(
        geometry.meshlets().size() > static_cast<usize>(Limit<u32>::s_Max)
        || geometry.meshletPrimitiveIndices().size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' meshlet payload exceeds u32 limits")
            , StringConvert(geometryPath.c_str())
        );
        return false;
    }

    GeometryResources createdGeometry;
    createdGeometry.geometryName = geometryPath;
    createdGeometry.meshletCount = static_cast<u32>(geometry.meshlets().size());
    createdGeometry.meshletPrimitiveIndexCount = static_cast<u32>(geometry.meshletPrimitiveIndices().size());

    bool uploaded = true;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<Float3U>(
        m_graphics,
        geometryPath,
        createdGeometry.positionBuffer,
        AStringView(":positions"),
        geometry.positionStream(),
        NWB_TEXT("position")
    ) && uploaded;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<Half4U>(
        m_graphics,
        geometryPath,
        createdGeometry.normalBuffer,
        AStringView(":normals"),
        geometry.normalStream(),
        NWB_TEXT("normal")
    ) && uploaded;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<Half4U>(
        m_graphics,
        geometryPath,
        createdGeometry.tangentBuffer,
        AStringView(":tangents"),
        geometry.tangentStream(),
        NWB_TEXT("tangent")
    ) && uploaded;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<Float2U>(
        m_graphics,
        geometryPath,
        createdGeometry.uv0Buffer,
        AStringView(":uv0"),
        geometry.uv0Stream(),
        NWB_TEXT("uv0")
    ) && uploaded;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<Half4U>(
        m_graphics,
        geometryPath,
        createdGeometry.colorBuffer,
        AStringView(":colors"),
        geometry.colorStream(),
        NWB_TEXT("color")
    ) && uploaded;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<GeometryVertexRef>(
        m_graphics,
        geometryPath,
        createdGeometry.vertexRefBuffer,
        AStringView(":vertex_refs"),
        geometry.vertexRefs(),
        NWB_TEXT("vertex ref")
    ) && uploaded;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<GeometryMeshletDesc>(
        m_graphics,
        geometryPath,
        createdGeometry.meshletDescBuffer,
        AStringView(":meshlets"),
        geometry.meshlets(),
        NWB_TEXT("meshlet descriptor")
    ) && uploaded;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<GeometryMeshletBounds>(
        m_graphics,
        geometryPath,
        createdGeometry.meshletBoundsBuffer,
        AStringView(":meshlet_bounds"),
        geometry.meshletBounds(),
        NWB_TEXT("meshlet bounds")
    ) && uploaded;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<u32>(
        m_graphics,
        geometryPath,
        createdGeometry.meshletVertexRefBuffer,
        AStringView(":meshlet_vertex_refs"),
        geometry.meshletVertexRefs(),
        NWB_TEXT("meshlet vertex ref")
    ) && uploaded;
    uploaded = __hidden_renderer_system_geometry::AssignGeometryBuffer<u8>(
        m_graphics,
        geometryPath,
        createdGeometry.meshletPrimitiveIndexBuffer,
        AStringView(":meshlet_primitive_indices"),
        geometry.meshletPrimitiveIndices(),
        NWB_TEXT("meshlet primitive index"),
        true
    ) && uploaded;
    if(!uploaded || !createdGeometry.valid())
        return false;

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
    createdGeometry.positionBuffer = desc.positionBuffer;
    createdGeometry.normalBuffer = desc.normalBuffer;
    createdGeometry.tangentBuffer = desc.tangentBuffer;
    createdGeometry.uv0Buffer = desc.uv0Buffer;
    createdGeometry.colorBuffer = desc.colorBuffer;
    createdGeometry.vertexRefBuffer = desc.vertexRefBuffer;
    createdGeometry.meshletDescBuffer = desc.meshletDescBuffer;
    createdGeometry.meshletBoundsBuffer = desc.meshletBoundsBuffer;
    createdGeometry.meshletVertexRefBuffer = desc.meshletVertexRefBuffer;
    createdGeometry.meshletPrimitiveIndexBuffer = desc.meshletPrimitiveIndexBuffer;
    createdGeometry.meshletCount = desc.meshletCount;
    createdGeometry.runtimeGeometry = true;
    createdGeometry.runtimeGeometryVersion = desc.version;
    if(!__hidden_renderer_system_geometry::ResolveBufferElementCount(
        createdGeometry.meshletPrimitiveIndexBuffer,
        createdGeometry.meshletPrimitiveIndexCount,
        createdGeometry.geometryName,
        NWB_TEXT("meshlet primitive index")
    ))
        return false;
    if(!createdGeometry.valid())
        return false;

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

void RendererSystem::addGeometrySourceBindingItems(Core::BindingSetDesc& bindingSetDesc, const GeometryResources& geometry)const{
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, geometry.positionBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, geometry.normalBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, geometry.tangentBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, geometry.uv0Buffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(4, geometry.colorBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(5, geometry.vertexRefBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(6, geometry.meshletDescBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(7, geometry.meshletBoundsBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(8, geometry.meshletVertexRefBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(9, geometry.meshletPrimitiveIndexBuffer.get()));
}

void RendererSystem::addGeometryFrameBindingItems(Core::BindingSetDesc& bindingSetDesc)const{
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(10, m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(11, m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(12, m_materialTypedBuffer.get()));
}

void RendererSystem::addGeometryDrawBindingItems(Core::BindingSetDesc& bindingSetDesc, const GeometryResources& geometry)const{
    addGeometrySourceBindingItems(bindingSetDesc, geometry);
    addGeometryFrameBindingItems(bindingSetDesc);
}

bool RendererSystem::geometryFrameBindingResourcesReady(const tchar* context)const{
    if(!m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires an instance buffer"), context);
        return false;
    }
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires a mesh view buffer"), context);
        return false;
    }
    if(!m_materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} requires a material typed buffer"), context);
        return false;
    }

    return true;
}

bool RendererSystem::createMeshBindingSet(GeometryResources& geometry){
    if(geometry.meshBindingSet)
        return true;
    if(!createMeshShaderResources())
        return false;
    if(!geometryFrameBindingResourcesReady(NWB_TEXT("mesh binding set")))
        return false;

    Core::BindingSetDesc bindingSetDesc(m_arena);
    addGeometryDrawBindingItems(bindingSetDesc, geometry);

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
    if(!geometryFrameBindingResourcesReady(NWB_TEXT("compute binding set")))
        return false;

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
            .setByteSize(static_cast<u64>(geometry.meshletPrimitiveIndexCount) * ECSRenderDetail::s_EmulatedVertexStride)
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
    addGeometryDrawBindingItems(bindingSetDesc, geometry);
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(13, geometry.emulationVertexBuffer.get()));

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

