// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_setup_async{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UploadBytes = Vector<u8, Alloc::GlobalArena>;


struct BufferSetupJobData{
    Graphics::BufferSetupDesc setupDesc;
    UploadBytes uploadBytes;
    BufferHandle& outBuffer;


    BufferSetupJobData(Alloc::GlobalArena& arena, const Graphics::BufferSetupDesc& desc, BufferHandle& output)
        : setupDesc(desc)
        , uploadBytes(arena)
        , outBuffer(output)
    {}
};

struct TextureSetupJobData{
    Graphics::TextureSetupDesc setupDesc;
    UploadBytes uploadBytes;
    TextureHandle& outTexture;


    TextureSetupJobData(Alloc::GlobalArena& arena, const Graphics::TextureSetupDesc& desc, TextureHandle& output)
        : setupDesc(desc)
        , uploadBytes(arena)
        , outTexture(output)
    {}
};

struct MeshSetupJobData{
    Graphics::MeshSetupDesc setupDesc;
    UploadBytes vertexBytes;
    UploadBytes indexBytes;
    Graphics::MeshResource& outMesh;


    MeshSetupJobData(Alloc::GlobalArena& arena, const Graphics::MeshSetupDesc& desc, Graphics::MeshResource& output)
        : setupDesc(desc)
        , vertexBytes(arena)
        , indexBytes(arena)
        , outMesh(output)
    {}
};


[[nodiscard]] static UploadBytes CopyBytes(Alloc::GlobalArena& arena, const void* data, const usize dataSize){
    UploadBytes bytes{arena};
    if(!data || dataSize == 0)
        return bytes;

    const u8* const byteData = static_cast<const u8*>(data);
    bytes.assign(byteData, byteData + dataSize);

    return bytes;
}

template<typename JobData, typename Desc, typename Output, typename Validate, typename ConfigurePayload, typename ExecutePayload>
[[nodiscard]] static Graphics::JobHandle SubmitSetupUploadJob(
    Graphics& graphics,
    Alloc::GlobalArena& arena,
    Alloc::JobSystem& jobSystem,
    const Desc& desc,
    Output& output,
    Validate&& validate,
    ConfigurePayload&& configurePayload,
    ExecutePayload&& executePayload
){
    if(!validate(desc)){
        output = nullptr;
        return {};
    }

    auto payload = MakeGlobalUnique<JobData>(arena, arena, desc, output);
    configurePayload(*payload, arena);

    return jobSystem.submit([&graphics, payload = Move(payload), executePayload = Forward<ExecutePayload>(executePayload)]() mutable{
        executePayload(graphics, *payload);
    });
}

static void ConfigureBufferSetupPayload(BufferSetupJobData& payload, Alloc::GlobalArena& arena){
    payload.uploadBytes = CopyBytes(arena, payload.setupDesc.data, payload.setupDesc.dataSize);
    payload.setupDesc.data = nullptr;
    payload.setupDesc.dataSize = payload.uploadBytes.size();
}

static void ExecuteBufferSetupPayload(Graphics& graphics, BufferSetupJobData& payload){
    payload.setupDesc.data = payload.uploadBytes.empty() ? nullptr : payload.uploadBytes.data();
    payload.setupDesc.dataSize = payload.uploadBytes.size();
    payload.outBuffer = graphics.setupBuffer(payload.setupDesc);
}

static void ConfigureTextureSetupPayload(TextureSetupJobData& payload, Alloc::GlobalArena& arena){
    payload.uploadBytes = CopyBytes(arena, payload.setupDesc.data, payload.setupDesc.uploadDataSize);
    payload.setupDesc.data = nullptr;
    payload.setupDesc.uploadDataSize = payload.uploadBytes.size();
}

static void ExecuteTextureSetupPayload(Graphics& graphics, TextureSetupJobData& payload){
    payload.setupDesc.data = payload.uploadBytes.empty() ? nullptr : payload.uploadBytes.data();
    payload.setupDesc.uploadDataSize = payload.uploadBytes.size();
    payload.outTexture = graphics.setupTexture(payload.setupDesc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Graphics::JobHandle Graphics::setupBufferAsync(const BufferSetupDesc& desc, BufferHandle& outBuffer){
    return __hidden_graphics_setup_async::SubmitSetupUploadJob<__hidden_graphics_setup_async::BufferSetupJobData>(
        *this,
        m_allocator.getObjectArena(),
        m_jobSystem,
        desc,
        outBuffer,
        GraphicsModuleDetail::ValidateBufferSetupUpload,
        __hidden_graphics_setup_async::ConfigureBufferSetupPayload,
        __hidden_graphics_setup_async::ExecuteBufferSetupPayload
    );
}

Graphics::JobHandle Graphics::setupTextureAsync(const TextureSetupDesc& desc, TextureHandle& outTexture){
    return __hidden_graphics_setup_async::SubmitSetupUploadJob<__hidden_graphics_setup_async::TextureSetupJobData>(
        *this,
        m_allocator.getObjectArena(),
        m_jobSystem,
        desc,
        outTexture,
        GraphicsModuleDetail::ValidateTextureSetupUpload,
        __hidden_graphics_setup_async::ConfigureTextureSetupPayload,
        __hidden_graphics_setup_async::ExecuteTextureSetupPayload
    );
}

Graphics::JobHandle Graphics::setupMeshAsync(const MeshSetupDesc& desc, MeshResource& outMesh){
    if(!GraphicsModuleDetail::ValidateMeshSetupDesc(desc)){
        outMesh = {};
        return {};
    }

    auto payload = MakeGlobalUnique<__hidden_graphics_setup_async::MeshSetupJobData>(
        m_allocator.getObjectArena(),
        m_allocator.getObjectArena(),
        desc,
        outMesh
    );
    payload->vertexBytes = __hidden_graphics_setup_async::CopyBytes(m_allocator.getObjectArena(), desc.vertexData, desc.vertexDataSize);
    payload->indexBytes = __hidden_graphics_setup_async::CopyBytes(m_allocator.getObjectArena(), desc.indexData, desc.indexDataSize);
    payload->setupDesc.vertexData = nullptr;
    payload->setupDesc.vertexDataSize = payload->vertexBytes.size();
    payload->setupDesc.indexData = nullptr;
    payload->setupDesc.indexDataSize = payload->indexBytes.size();

    return m_jobSystem.submit([this, payload = Move(payload)]() mutable{
        payload->setupDesc.vertexData = payload->vertexBytes.empty() ? nullptr : payload->vertexBytes.data();
        payload->setupDesc.vertexDataSize = payload->vertexBytes.size();
        payload->setupDesc.indexData = payload->indexBytes.empty() ? nullptr : payload->indexBytes.data();
        payload->setupDesc.indexDataSize = payload->indexBytes.size();
        payload->outMesh = setupMesh(payload->setupDesc);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

