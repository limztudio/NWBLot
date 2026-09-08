// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_setup_async{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UploadBytes = Vector<u8, Alloc::GlobalArena>;


// Small setup payloads favor efficiency workers; larger copies favor performance workers. Both retain normal
// priority because a caller may need the resulting resource in the current frame.
static constexpr usize s_HeavySetupUploadBytes = 64u * 1024u;


struct BufferSetupTaskData{
    GraphicsRuntime::BufferSetupDesc setupDesc;
    UploadBytes uploadBytes;
    BufferHandle& outBuffer;


    BufferSetupTaskData(Alloc::GlobalArena& arena, const GraphicsRuntime::BufferSetupDesc& desc, BufferHandle& output)
        : setupDesc(desc)
        , uploadBytes(arena)
        , outBuffer(output)
    {}
};

struct TextureSetupTaskData{
    GraphicsRuntime::TextureSetupDesc setupDesc;
    UploadBytes uploadBytes;
    TextureHandle& outTexture;


    TextureSetupTaskData(Alloc::GlobalArena& arena, const GraphicsRuntime::TextureSetupDesc& desc, TextureHandle& output)
        : setupDesc(desc)
        , uploadBytes(arena)
        , outTexture(output)
    {}
};

struct MeshSetupTaskData{
    GraphicsRuntime::MeshSetupDesc setupDesc;
    UploadBytes vertexBytes;
    UploadBytes indexBytes;
    GraphicsRuntime::MeshResource& outMesh;


    MeshSetupTaskData(Alloc::GlobalArena& arena, const GraphicsRuntime::MeshSetupDesc& desc, GraphicsRuntime::MeshResource& output)
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

[[nodiscard]] static CpuTaskOptions SetupUploadTaskOptions(const usize firstUploadBytes, const usize secondUploadBytes = 0u)noexcept{
    const bool heavy = firstUploadBytes >= s_HeavySetupUploadBytes
        || secondUploadBytes >= s_HeavySetupUploadBytes - firstUploadBytes;
    return CpuTaskOptions{ .cost = heavy ? CpuTaskCost::Heavy : CpuTaskCost::Light };
}


template<typename TaskData, typename Desc, typename Output, typename Validate, typename ConfigurePayload, typename ExecutePayload>
[[nodiscard]] static GraphicsRuntime::TaskHandle SubmitSetupUploadTask(
    GraphicsRuntime& graphics,
    Alloc::GlobalArena& arena,
    CpuTaskScope& tasks,
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

    auto payload = MakeGlobalUnique<TaskData>(arena, arena, desc, output);
    configurePayload(*payload, arena);
    const CpuTaskOptions options = SetupUploadTaskOptions(payload->uploadBytes.size());

    return tasks.submit([&graphics, payload = Move(payload), executePayload = Forward<ExecutePayload>(executePayload)]() mutable{
        executePayload(graphics, *payload);
    }, options);
}

static void ConfigureBufferSetupPayload(BufferSetupTaskData& payload, Alloc::GlobalArena& arena){
    payload.uploadBytes = CopyBytes(arena, payload.setupDesc.data, payload.setupDesc.dataSize);
    payload.setupDesc.data = nullptr;
    payload.setupDesc.dataSize = payload.uploadBytes.size();
}

static void ExecuteBufferSetupPayload(GraphicsRuntime& graphics, BufferSetupTaskData& payload){
    payload.setupDesc.data = payload.uploadBytes.empty() ? nullptr : payload.uploadBytes.data();
    payload.setupDesc.dataSize = payload.uploadBytes.size();
    payload.outBuffer = graphics.setupBuffer(payload.setupDesc);
}

static void ConfigureTextureSetupPayload(TextureSetupTaskData& payload, Alloc::GlobalArena& arena){
    payload.uploadBytes = CopyBytes(arena, payload.setupDesc.data, payload.setupDesc.uploadDataSize);
    payload.setupDesc.data = nullptr;
    payload.setupDesc.uploadDataSize = payload.uploadBytes.size();
}

static void ExecuteTextureSetupPayload(GraphicsRuntime& graphics, TextureSetupTaskData& payload){
    payload.setupDesc.data = payload.uploadBytes.empty() ? nullptr : payload.uploadBytes.data();
    payload.setupDesc.uploadDataSize = payload.uploadBytes.size();
    payload.outTexture = graphics.setupTexture(payload.setupDesc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GraphicsRuntime::TaskHandle GraphicsRuntime::setupBufferAsync(const BufferSetupDesc& desc, BufferHandle& outBuffer){
    return __hidden_graphics_setup_async::SubmitSetupUploadTask<__hidden_graphics_setup_async::BufferSetupTaskData>(
        *this,
        m_allocator.getObjectArena(),
        m_tasks,
        desc,
        outBuffer,
        GraphicsModuleDetail::ValidateBufferSetupUpload,
        __hidden_graphics_setup_async::ConfigureBufferSetupPayload,
        __hidden_graphics_setup_async::ExecuteBufferSetupPayload
    );
}

GraphicsRuntime::TaskHandle GraphicsRuntime::setupTextureAsync(const TextureSetupDesc& desc, TextureHandle& outTexture){
    return __hidden_graphics_setup_async::SubmitSetupUploadTask<__hidden_graphics_setup_async::TextureSetupTaskData>(
        *this,
        m_allocator.getObjectArena(),
        m_tasks,
        desc,
        outTexture,
        GraphicsModuleDetail::ValidateTextureSetupUpload,
        __hidden_graphics_setup_async::ConfigureTextureSetupPayload,
        __hidden_graphics_setup_async::ExecuteTextureSetupPayload
    );
}

GraphicsRuntime::TaskHandle GraphicsRuntime::setupMeshAsync(const MeshSetupDesc& desc, MeshResource& outMesh){
    if(!GraphicsModuleDetail::ValidateMeshSetupDesc(desc)){
        outMesh = {};
        return {};
    }

    auto payload = MakeGlobalUnique<__hidden_graphics_setup_async::MeshSetupTaskData>(
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

    const CpuTaskOptions options = __hidden_graphics_setup_async::SetupUploadTaskOptions(
        payload->vertexBytes.size(),
        payload->indexBytes.size()
    );
    return m_tasks.submit([this, payload = Move(payload)]() mutable{
        payload->setupDesc.vertexData = payload->vertexBytes.empty() ? nullptr : payload->vertexBytes.data();
        payload->setupDesc.vertexDataSize = payload->vertexBytes.size();
        payload->setupDesc.indexData = payload->indexBytes.empty() ? nullptr : payload->indexBytes.data();
        payload->setupDesc.indexDataSize = payload->indexBytes.size();
        payload->outMesh = setupMesh(payload->setupDesc);
    }, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

