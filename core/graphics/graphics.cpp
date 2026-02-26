// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics.h"

#include <logger/client/logger.h>

#include "vulkan/vulkan_device_manager.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UploadBytesAllocator = Alloc::CustomAllocator<u8>;
using UploadBytes = Vector<u8, UploadBytesAllocator>;


static UploadBytes CopyBytes(Alloc::CustomArena& arena, const void* data, usize dataSize){
    UploadBytes bytes{UploadBytesAllocator(arena)};
    if(data && dataSize > 0){
        bytes.resize(dataSize);
        NWB_MEMCPY(bytes.data(), dataSize, data, dataSize);
    }

    return bytes;
}

static void AddVulkanDeviceExtensionOnce(Vector<AString>& extensions, const char* extensionName){
    for(const auto& extension : extensions){
        if(NWB_STRCMP(extension.c_str(), extensionName) == 0)
            return;
    }
    extensions.push_back(extensionName);
}

constexpr bool IsFp16CoopVecFormat(const CooperativeVectorMatMulFormatCombo& combo){
    return combo.inputType == CooperativeVectorDataType::Float16 &&
           combo.inputInterpretation == CooperativeVectorDataType::Float16 &&
           combo.matrixInterpretation == CooperativeVectorDataType::Float16 &&
           combo.outputType == CooperativeVectorDataType::Float16;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IDeviceManager* IDeviceManager::create(GraphicsAPI::Enum api, const DeviceCreationParameters& params){
    switch(api){
    case GraphicsAPI::VULKAN:
        return new Vulkan::DeviceManager(params);
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("DeviceManager: Unsupported graphics API."));
        return nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Graphics::Graphics(GraphicsAllocator& allocator, Alloc::ThreadPool& threadPool, Alloc::JobSystem& jobSystem)
    : m_allocator(allocator)
    , m_threadPool(threadPool)
    , m_jobSystem(jobSystem)
{
    m_deviceCreationParams.allocator = &m_allocator;
    m_deviceCreationParams.threadPool = &m_threadPool;
    __hidden_graphics::AddVulkanDeviceExtensionOnce(m_deviceCreationParams.optionalVulkanDeviceExtensions, "VK_NV_cooperative_vector");
    m_deviceManager.reset(IDeviceManager::create(GraphicsAPI::VULKAN, m_deviceCreationParams));
    NWB_ASSERT_MSG(m_deviceManager != nullptr, NWB_TEXT("Graphics: DeviceManager creation failed."));
}
Graphics::~Graphics(){
    destroy();
}

bool Graphics::init(const Common::FrameData& data){
    NWB_ASSERT_MSG(m_deviceManager != nullptr, NWB_TEXT("Graphics::init requires a valid DeviceManager."));
    return m_deviceManager->createWindowDeviceAndSwapChain(data);
}

bool Graphics::runFrame(){
    NWB_ASSERT_MSG(m_deviceManager != nullptr, NWB_TEXT("Graphics::runFrame requires a valid DeviceManager."));
    return m_deviceManager->runFrame();
}

void Graphics::updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus){
    NWB_ASSERT_MSG(m_deviceManager != nullptr, NWB_TEXT("Graphics::updateWindowState requires a valid DeviceManager."));
    m_deviceManager->updateWindowState(width, height, windowVisible, windowIsInFocus);
}

void Graphics::destroy(){
    waitAllJobs();

    if(m_deviceManager){
        m_deviceManager->shutdown();
        m_deviceManager.reset();
    }
}

IDevice* Graphics::getDevice()const noexcept{
    NWB_ASSERT_MSG(m_deviceManager != nullptr, NWB_TEXT("Graphics::getDevice requires a valid DeviceManager."));
    return m_deviceManager->getDevice();
}

BufferHandle Graphics::createBuffer(const BufferDesc& desc)const{
    return getDevice()->createBuffer(desc);
}

TextureHandle Graphics::createTexture(const TextureDesc& desc)const{
    return getDevice()->createTexture(desc);
}

BufferHandle Graphics::setupBuffer(const BufferSetupDesc& desc)const{
    IDevice* device = getDevice();

    BufferHandle buffer = device->createBuffer(desc.bufferDesc);
    if(!buffer)
        return {};

    if(!desc.data || desc.dataSize == 0)
        return buffer;

    CommandListParameters cmdParams;
    cmdParams.setQueueType(desc.queue);
    CommandListHandle commandList = device->createCommandList(cmdParams);
    if(!commandList)
        return {};

    commandList->open();
    commandList->writeBuffer(buffer.get(), desc.data, desc.dataSize, desc.destOffsetBytes);
    commandList->close();
    device->executeCommandList(commandList.get(), desc.queue);

    return buffer;
}

TextureHandle Graphics::setupTexture(const TextureSetupDesc& desc)const{
    IDevice* device = getDevice();

    TextureHandle texture = device->createTexture(desc.textureDesc);
    if(!texture)
        return {};

    if(!desc.data || desc.uploadDataSize == 0)
        return texture;

    CommandListParameters cmdParams;
    cmdParams.setQueueType(desc.queue);
    CommandListHandle commandList = device->createCommandList(cmdParams);
    if(!commandList)
        return {};

    commandList->open();
    commandList->writeTexture(texture.get(), desc.arraySlice, desc.mipLevel, desc.data, desc.rowPitch, desc.depthPitch);
    commandList->close();
    device->executeCommandList(commandList.get(), desc.queue);

    return texture;
}

Graphics::MeshResource Graphics::setupMesh(const MeshSetupDesc& desc)const{
    MeshResource output;
    output.vertexStride = desc.vertexStride;

    if(desc.vertexData && desc.vertexDataSize > 0){
        BufferDesc vertexBufferDesc;
        vertexBufferDesc.setByteSize(static_cast<u64>(desc.vertexDataSize));
        vertexBufferDesc.setIsVertexBuffer(true);
        vertexBufferDesc.setDebugName(desc.vertexBufferName);
        vertexBufferDesc.enableAutomaticStateTracking(ResourceStates::VertexBuffer);

        BufferSetupDesc vertexSetup;
        vertexSetup.bufferDesc = vertexBufferDesc;
        vertexSetup.data = desc.vertexData;
        vertexSetup.dataSize = desc.vertexDataSize;
        vertexSetup.queue = desc.queue;

        output.vertexBuffer = setupBuffer(vertexSetup);
        if(!output.vertexBuffer)
            return MeshResource{};
    }

    if(desc.indexData && desc.indexDataSize > 0){
        BufferDesc indexBufferDesc;
        indexBufferDesc.setByteSize(static_cast<u64>(desc.indexDataSize));
        indexBufferDesc.setIsIndexBuffer(true);
        indexBufferDesc.setDebugName(desc.indexBufferName);
        indexBufferDesc.enableAutomaticStateTracking(ResourceStates::IndexBuffer);

        BufferSetupDesc indexSetup;
        indexSetup.bufferDesc = indexBufferDesc;
        indexSetup.data = desc.indexData;
        indexSetup.dataSize = desc.indexDataSize;
        indexSetup.queue = desc.queue;

        output.indexBuffer = setupBuffer(indexSetup);
        if(!output.indexBuffer)
            return MeshResource{};
    }

    if(output.vertexStride > 0 && desc.vertexDataSize > 0)
        output.vertexCount = static_cast<u32>(desc.vertexDataSize / static_cast<usize>(output.vertexStride));

    if(desc.indexDataSize > 0){
        const usize indexStride = desc.use32BitIndices ? sizeof(u32) : sizeof(u16);
        output.indexFormat = desc.use32BitIndices ? Format::R32_UINT : Format::R16_UINT;
        output.indexCount = static_cast<u32>(desc.indexDataSize / indexStride);
    }

    return output;
}

Graphics::JobHandle Graphics::setupBufferAsync(const BufferSetupDesc& desc, BufferHandle* outBuffer){
    if(!outBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics::setupBufferAsync requires a valid output pointer."));
        return JobHandle{};
    }

    BufferSetupDesc setupDesc = desc;
    __hidden_graphics::UploadBytes uploadBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.data, desc.dataSize);
    setupDesc.data = nullptr;
    setupDesc.dataSize = uploadBytes.size();

    return m_jobSystem.submit([this, setupDesc, uploadBytes = Move(uploadBytes), outBuffer]() mutable{
        BufferSetupDesc jobDesc = setupDesc;
        jobDesc.data = uploadBytes.empty() ? nullptr : uploadBytes.data();
        jobDesc.dataSize = uploadBytes.size();
        *outBuffer = setupBuffer(jobDesc);
    });
}

Graphics::JobHandle Graphics::setupTextureAsync(const TextureSetupDesc& desc, TextureHandle* outTexture){
    if(!outTexture){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics::setupTextureAsync requires a valid output pointer."));
        return JobHandle{};
    }

    TextureSetupDesc setupDesc = desc;
    __hidden_graphics::UploadBytes uploadBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.data, desc.uploadDataSize);
    setupDesc.data = nullptr;
    setupDesc.uploadDataSize = uploadBytes.size();

    return m_jobSystem.submit([this, setupDesc, uploadBytes = Move(uploadBytes), outTexture]() mutable{
        TextureSetupDesc jobDesc = setupDesc;
        jobDesc.data = uploadBytes.empty() ? nullptr : uploadBytes.data();
        jobDesc.uploadDataSize = uploadBytes.size();
        *outTexture = setupTexture(jobDesc);
    });
}

Graphics::JobHandle Graphics::setupMeshAsync(const MeshSetupDesc& desc, MeshResource* outMesh){
    if(!outMesh){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics::setupMeshAsync requires a valid output pointer."));
        return JobHandle{};
    }

    MeshSetupDesc setupDesc = desc;
    __hidden_graphics::UploadBytes vertexBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.vertexData, desc.vertexDataSize);
    __hidden_graphics::UploadBytes indexBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.indexData, desc.indexDataSize);
    setupDesc.vertexData = nullptr;
    setupDesc.vertexDataSize = vertexBytes.size();
    setupDesc.indexData = nullptr;
    setupDesc.indexDataSize = indexBytes.size();

    return m_jobSystem.submit([this, setupDesc, vertexBytes = Move(vertexBytes), indexBytes = Move(indexBytes), outMesh]() mutable{
        MeshSetupDesc jobDesc = setupDesc;
        jobDesc.vertexData = vertexBytes.empty() ? nullptr : vertexBytes.data();
        jobDesc.vertexDataSize = vertexBytes.size();
        jobDesc.indexData = indexBytes.empty() ? nullptr : indexBytes.data();
        jobDesc.indexDataSize = indexBytes.size();
        *outMesh = setupMesh(jobDesc);
    });
}

Graphics::CoopVectorSupport Graphics::queryCoopVecSupport()const{
    CoopVectorSupport output;

    IDevice* device = getDevice();

    output.inferencingSupported = device->queryFeatureSupport(Feature::CooperativeVectorInferencing);
    output.trainingSupported = device->queryFeatureSupport(Feature::CooperativeVectorTraining);

    const CooperativeVectorDeviceFeatures features = device->queryCoopVecFeatures();
    output.fp32TrainingSupported = features.trainingFloat32;

    for(const auto& combo : features.matMulFormats){
        if(__hidden_graphics::IsFp16CoopVecFormat(combo)){
            output.fp16InferencingSupported = true;
            output.fp16TrainingSupported = features.trainingFloat16;
            break;
        }
    }

    return output;
}

CooperativeVectorDeviceFeatures Graphics::queryCoopVecFeatures()const{
    IDevice* device = getDevice();
    return device->queryCoopVecFeatures();
}

usize Graphics::getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, int rows, int columns)const{
    IDevice* device = getDevice();
    return device->getCoopVecMatrixSize(type, layout, rows, columns);
}

void Graphics::waitJob(JobHandle handle)const{
    if(!handle.isValid())
        return;

    m_jobSystem.wait(handle);
}

void Graphics::waitAllJobs()const{
    m_jobSystem.waitAll();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

