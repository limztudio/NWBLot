// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics.h"

#include <logger/client/logger.h>

#include "vulkan/vulkan_backend_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UploadBytesAllocator = Alloc::CustomAllocator<u8>;
using UploadBytes = Vector<u8, UploadBytesAllocator>;


struct BufferSetupJobData{
    Graphics::BufferSetupDesc setupDesc;
    UploadBytes uploadBytes;
    BufferHandle& outBuffer;


    BufferSetupJobData(Alloc::CustomArena& arena, const Graphics::BufferSetupDesc& desc, BufferHandle& output)
        : setupDesc(desc)
        , uploadBytes(UploadBytesAllocator(arena))
        , outBuffer(output)
    {}
};

struct TextureSetupJobData{
    Graphics::TextureSetupDesc setupDesc;
    UploadBytes uploadBytes;
    TextureHandle& outTexture;


    TextureSetupJobData(Alloc::CustomArena& arena, const Graphics::TextureSetupDesc& desc, TextureHandle& output)
        : setupDesc(desc)
        , uploadBytes(UploadBytesAllocator(arena))
        , outTexture(output)
    {}
};

struct MeshSetupJobData{
    Graphics::MeshSetupDesc setupDesc;
    UploadBytes vertexBytes;
    UploadBytes indexBytes;
    Graphics::MeshResource& outMesh;


    MeshSetupJobData(Alloc::CustomArena& arena, const Graphics::MeshSetupDesc& desc, Graphics::MeshResource& output)
        : setupDesc(desc)
        , vertexBytes(UploadBytesAllocator(arena))
        , indexBytes(UploadBytesAllocator(arena))
        , outMesh(output)
    {}
};


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

static void CopyInstanceParameters(DeviceCreationParameters& dst, const InstanceParameters& src){
    static_cast<InstanceParameters&>(dst) = src;
}

static UniquePtr<IGraphicsBackend> CreateDefaultBackend(
    const DeviceCreationParameters& deviceParams,
    SwapChainRuntimeState& swapChainState,
    GraphicsAllocator& allocator,
    Alloc::ThreadPool& threadPool
)
{
    return UniquePtr<IGraphicsBackend>(new Vulkan::BackendContext(deviceParams, swapChainState, allocator, threadPool));
}

static const Vulkan::IBackendQueries* GetVulkanBackendQueries(const IGraphicsBackend* backend){
    if(!backend || backend->getGraphicsAPI() != GraphicsAPI::VULKAN)
        return nullptr;

    return static_cast<const Vulkan::IBackendQueries*>(backend->queryInterface(Vulkan::s_BackendQueriesInterfaceID));
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


Graphics::Graphics(GraphicsAllocator& allocator, Alloc::ThreadPool& threadPool, Alloc::JobSystem& jobSystem)
    : m_allocator(allocator)
    , m_threadPool(threadPool)
    , m_jobSystem(jobSystem)
{
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;
    __hidden_graphics::AddVulkanDeviceExtensionOnce(m_deviceCreationParams.optionalVulkanDeviceExtensions, "VK_NV_cooperative_vector");
    m_backend = __hidden_graphics::CreateDefaultBackend(m_deviceCreationParams, m_swapChainState, m_allocator, m_threadPool);
    NWB_FATAL_ASSERT_MSG(m_backend != nullptr, NWB_TEXT("Graphics: Vulkan backend creation failed."));
}
Graphics::~Graphics(){
    destroy();
}

IGraphicsBackend& Graphics::ensureBackend(){
    if(!m_backend)
        m_backend = __hidden_graphics::CreateDefaultBackend(m_deviceCreationParams, m_swapChainState, m_allocator, m_threadPool);

    NWB_FATAL_ASSERT_MSG(m_backend != nullptr, NWB_TEXT("Graphics: Vulkan backend creation failed."));
    return *m_backend;
}

IGraphicsBackend& Graphics::requireBackend()const noexcept{
    NWB_FATAL_ASSERT_MSG(m_backend != nullptr, NWB_TEXT("Graphics requires a valid backend."));
    return *m_backend;
}

bool Graphics::init(const Common::FrameData& data){
    m_deviceCreationParams.headlessDevice = false;
    m_hasPresentedFrame = false;

    m_swapChainState.backBufferWidth = data.width();
    m_swapChainState.backBufferHeight = data.height();
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;

    IGraphicsBackend& backend = ensureBackend();
    backend.setPlatformFrameParam(data.frameParam());

    if(!m_instanceCreated){
        if(!backend.createInstance())
            return false;
        m_instanceCreated = true;
    }

    if(!backend.createDevice())
        return false;

    if(!backend.createSwapChain())
        return false;

    m_swapChainState.backBufferWidth = 0;
    m_swapChainState.backBufferHeight = 0;
    updateWindowState(data.width(), data.height(), true, true);
    m_previousFrameTimestamp = TimerNow();

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Graphics: window device and swap chain created ({}x{})"),
        data.width(),
        data.height()
    );
    return true;
}

bool Graphics::createHeadlessDevice(){
    m_deviceCreationParams.headlessDevice = true;
    m_hasPresentedFrame = false;

    IGraphicsBackend& backend = ensureBackend();
    if(!m_instanceCreated){
        if(!backend.createInstance())
            return false;
        m_instanceCreated = true;
    }

    if(!backend.createDevice())
        return false;

    m_previousFrameTimestamp = TimerNow();

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Graphics: headless device created"));
    return true;
}

bool Graphics::createInstance(const InstanceParameters& params){
    __hidden_graphics::CopyInstanceParameters(m_deviceCreationParams, params);

    if(!ensureBackend().createInstance())
        return false;

    m_instanceCreated = true;
    return true;
}

bool Graphics::runFrame(){
    return animateRenderPresent();
}

void Graphics::updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus){
    m_windowVisible = windowVisible;
    m_windowIsInFocus = windowIsInFocus;

    if(!m_windowVisible)
        return;

    if(width == 0 || height == 0){
        m_windowVisible = false;
        return;
    }

    if(
        static_cast<i32>(m_swapChainState.backBufferWidth) != static_cast<i32>(width)
        || static_cast<i32>(m_swapChainState.backBufferHeight) != static_cast<i32>(height)
        || (m_swapChainState.vsyncEnabled != m_requestedVSync && getGraphicsAPI() == GraphicsAPI::VULKAN)
    )
    {
        backBufferResizing();

        m_swapChainState.backBufferWidth = width;
        m_swapChainState.backBufferHeight = height;
        m_swapChainState.vsyncEnabled = m_requestedVSync;

        requireBackend().resizeSwapChain();
        backBufferResized();
    }

    m_swapChainState.vsyncEnabled = m_requestedVSync;
}

void Graphics::destroy(){
    waitAllJobs();

    if(m_backend){
        m_swapChainFramebuffers.clear();
        m_renderPasses.clear();
        m_backend->destroy();
        m_instanceCreated = false;
        m_backend.reset();
    }
}

IDevice* Graphics::getDevice()const noexcept{
    return requireBackend().getDevice();
}

bool Graphics::enumerateAdapters(Vector<AdapterInfo>& outAdapters){
    if(!m_backend)
        return false;

    return m_backend->enumerateAdapters(outAdapters);
}

void Graphics::addRenderPassToFront(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_front(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
}

void Graphics::addRenderPassToBack(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_back(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
}

void Graphics::removeRenderPass(IRenderPass& pass){
    m_renderPasses.remove(&pass);
}

const tchar* Graphics::getRendererString()const{
    return requireBackend().getRendererString();
}

GraphicsAPI::Enum Graphics::getGraphicsAPI()const{
    return requireBackend().getGraphicsAPI();
}

f64 Graphics::getPreviousFrameTimestamp()const{
    return DurationInSeconds<f64>(m_previousFrameTimestamp);
}

bool Graphics::isVsyncEnabled()const{
    return m_swapChainState.vsyncEnabled;
}

void Graphics::setVSyncEnabled(bool enabled){
    m_requestedVSync = enabled;
}

void Graphics::reportLiveObjects()const{
    if(m_backend)
        m_backend->reportLiveObjects();
}

void Graphics::getWindowDimensions(i32& width, i32& height)const{
    width = m_swapChainState.backBufferWidth;
    height = m_swapChainState.backBufferHeight;
}

void Graphics::getDPIScaleInfo(f32& x, f32& y)const{
    x = m_dpiScaleFactorX;
    y = m_dpiScaleFactorY;
}

const tchar* Graphics::getWindowTitle()const{
    return m_windowTitle.c_str();
}

void Graphics::setWindowTitle(NotNull<const tchar*> title){
    if(m_windowTitle == title.get())
        return;

    m_windowTitle = title.get();
}

ITexture* Graphics::getCurrentBackBuffer()const{
    return requireBackend().getCurrentBackBuffer();
}

ITexture* Graphics::getBackBuffer(u32 index)const{
    return requireBackend().getBackBuffer(index);
}

u32 Graphics::getCurrentBackBufferIndex()const{
    return requireBackend().getCurrentBackBufferIndex();
}

u32 Graphics::getBackBufferCount()const{
    return requireBackend().getBackBufferCount();
}

IFramebuffer* Graphics::getCurrentFramebuffer()const{
    return getFramebuffer(getCurrentBackBufferIndex());
}

IFramebuffer* Graphics::getFramebuffer(u32 index)const{
    if(index < m_swapChainFramebuffers.size())
        return m_swapChainFramebuffers[index].get();
    return nullptr;
}

bool Graphics::isVulkanInstanceExtensionEnabled(const char* extensionName)const{
    const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(m_backend.get());
    return queries && queries->isInstanceExtensionEnabled(extensionName);
}

bool Graphics::isVulkanDeviceExtensionEnabled(const char* extensionName)const{
    const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(m_backend.get());
    return queries && queries->isDeviceExtensionEnabled(extensionName);
}

bool Graphics::isVulkanLayerEnabled(const char* layerName)const{
    const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(m_backend.get());
    return queries && queries->isLayerEnabled(layerName);
}

void Graphics::getEnabledVulkanInstanceExtensions(Vector<AString>& extensions)const{
    if(const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(m_backend.get())){
        queries->getEnabledInstanceExtensions(extensions);
        return;
    }

    extensions.clear();
}

void Graphics::getEnabledVulkanDeviceExtensions(Vector<AString>& extensions)const{
    if(const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(m_backend.get())){
        queries->getEnabledDeviceExtensions(extensions);
        return;
    }

    extensions.clear();
}

void Graphics::getEnabledVulkanLayers(Vector<AString>& layers)const{
    if(const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(m_backend.get())){
        queries->getEnabledLayers(layers);
        return;
    }

    layers.clear();
}

void Graphics::keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods){
    if(!m_backend || key == -1)
        return;

    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->keyboardUpdate(key, scancode, action, mods))
            break;
    }
}

void Graphics::keyboardCharInput(u32 unicode, i32 mods){
    if(!m_backend)
        return;

    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->keyboardCharInput(unicode, mods))
            break;
    }
}

void Graphics::mousePosUpdate(f64 xpos, f64 ypos){
    if(!m_backend)
        return;

    if(!m_deviceCreationParams.supportExplicitDisplayScaling){
        xpos /= m_dpiScaleFactorX;
        ypos /= m_dpiScaleFactorY;
    }

    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->mousePosUpdate(xpos, ypos))
            break;
    }
}

void Graphics::mouseButtonUpdate(i32 button, i32 action, i32 mods){
    if(!m_backend)
        return;

    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->mouseButtonUpdate(button, action, mods))
            break;
    }
}

void Graphics::mouseScrollUpdate(f64 xoffset, f64 yoffset){
    if(!m_backend)
        return;

    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->mouseScrollUpdate(xoffset, yoffset))
            break;
    }
}

void Graphics::backBufferResizing(){
    m_swapChainFramebuffers.clear();

    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResizing();
}

void Graphics::backBufferResized(){
    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);

    const u32 backBufferCount = getBackBufferCount();
    m_swapChainFramebuffers.resize(backBufferCount);
    for(u32 index = 0; index < backBufferCount; ++index)
        m_swapChainFramebuffers[index] = getDevice()->createFramebuffer(FramebufferDesc().addColorAttachment(getBackBuffer(index)));

    NWB_LOGGER_INFO(NWB_TEXT("Graphics: Back buffer resized to {}x{}"), m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight);
}

void Graphics::displayScaleChanged(){
    for(auto* renderPass : m_renderPasses)
        renderPass->displayScaleChanged(m_dpiScaleFactorX, m_dpiScaleFactorY);
}

void Graphics::animate(f64 elapsedTime){
    for(auto* renderPass : m_renderPasses){
        renderPass->animate(static_cast<f32>(elapsedTime));
        renderPass->setLatewarpOptions();
    }
}

void Graphics::render(){
    IFramebuffer* framebuffer = getCurrentFramebuffer();

    for(auto* renderPass : m_renderPasses)
        renderPass->render(framebuffer);
}

void Graphics::updateAverageFrameTime(f64 elapsedTime){
    m_frameTimeSum += elapsedTime;
    m_numberOfAccumulatedFrames += 1;

    if(m_frameTimeSum > m_averageTimeUpdateInterval && m_numberOfAccumulatedFrames > 0){
        m_averageFrameTime = m_frameTimeSum / static_cast<f64>(m_numberOfAccumulatedFrames);
        m_numberOfAccumulatedFrames = 0;
        m_frameTimeSum = 0.0;
    }
}

bool Graphics::shouldRenderUnfocused()const{
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->shouldRenderUnfocused())
            return true;
    }
    return false;
}

void Graphics::BackBufferResizingCallback(void* userData){
    if(auto* graphics = static_cast<Graphics*>(userData))
        graphics->backBufferResizing();
}

void Graphics::BackBufferResizedCallback(void* userData){
    if(auto* graphics = static_cast<Graphics*>(userData))
        graphics->backBufferResized();
}

bool Graphics::animateRenderPresent(){
    Timer now = TimerNow();
    const f64 elapsedTime = DurationInSeconds<f64>(now, m_previousFrameTimestamp);
    const bool shouldBootstrapWindowPresentation = !m_hasPresentedFrame;

    if(m_windowVisible && (m_windowIsInFocus || shouldRenderUnfocused() || shouldBootstrapWindowPresentation)){
        if(m_prevDPIScaleFactorX != m_dpiScaleFactorX || m_prevDPIScaleFactorY != m_dpiScaleFactorY){
            displayScaleChanged();
            m_prevDPIScaleFactorX = m_dpiScaleFactorX;
            m_prevDPIScaleFactorY = m_dpiScaleFactorY;
        }

        animate(elapsedTime);

        if(m_frameIndex > 0 || !m_skipRenderOnFirstFrame){
            const BackBufferResizeCallbacks resizeCallbacks = {
                this,
                &Graphics::BackBufferResizingCallback,
                &Graphics::BackBufferResizedCallback,
            };
            if(requireBackend().beginFrame(resizeCallbacks)){
                render();

                if(!requireBackend().present())
                    return false;

                m_hasPresentedFrame = true;
            }
        }
    }

    yield();

    if(IDevice* device = getDevice())
        device->runGarbageCollection();

    updateAverageFrameTime(elapsedTime);
    m_previousFrameTimestamp = now;

    ++m_frameIndex;
    return true;
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
    if(!buffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Graphics: failed to create setup buffer '{}'"),
            StringConvert(desc.bufferDesc.debugName.c_str())
        );
        return {};
    }

    if(!desc.data || desc.dataSize == 0)
        return buffer;

    CommandListParameters cmdParams;
    cmdParams.setQueueType(desc.queue);
    CommandListHandle commandList = device->createCommandList(cmdParams);
    if(!commandList){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Graphics: failed to create upload command list for buffer '{}'"),
            StringConvert(desc.bufferDesc.debugName.c_str())
        );
        return {};
    }

    commandList->open();
    commandList->writeBuffer(buffer.get(), desc.data, desc.dataSize, desc.destOffsetBytes);
    commandList->close();
    device->executeCommandList(commandList.get(), desc.queue);

    return buffer;
}

TextureHandle Graphics::setupTexture(const TextureSetupDesc& desc)const{
    IDevice* device = getDevice();

    TextureHandle texture = device->createTexture(desc.textureDesc);
    if(!texture){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Graphics: failed to create setup texture '{}'"),
            StringConvert(desc.textureDesc.name.c_str())
        );
        return {};
    }

    if(!desc.data || desc.uploadDataSize == 0)
        return texture;

    CommandListParameters cmdParams;
    cmdParams.setQueueType(desc.queue);
    CommandListHandle commandList = device->createCommandList(cmdParams);
    if(!commandList){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Graphics: failed to create upload command list for texture '{}'"),
            StringConvert(desc.textureDesc.name.c_str())
        );
        return {};
    }

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
        if(!output.vertexBuffer){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Graphics: failed to set up mesh vertex buffer '{}'"),
                StringConvert(desc.vertexBufferName.c_str())
            );
            return MeshResource{};
        }
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
        if(!output.indexBuffer){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Graphics: failed to set up mesh index buffer '{}'"),
                StringConvert(desc.indexBufferName.c_str())
            );
            return MeshResource{};
        }
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

Graphics::JobHandle Graphics::setupBufferAsync(const BufferSetupDesc& desc, BufferHandle& outBuffer){
    auto payload = MakeCustomUnique<__hidden_graphics::BufferSetupJobData>(
        m_allocator.getObjectArena(),
        m_allocator.getObjectArena(),
        desc,
        outBuffer
    );
    payload->uploadBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.data, desc.dataSize);
    payload->setupDesc.data = nullptr;
    payload->setupDesc.dataSize = payload->uploadBytes.size();

    return m_jobSystem.submit([this, payload = Move(payload)]() mutable{
        payload->setupDesc.data = payload->uploadBytes.empty() ? nullptr : payload->uploadBytes.data();
        payload->setupDesc.dataSize = payload->uploadBytes.size();
        payload->outBuffer = setupBuffer(payload->setupDesc);
    });
}

Graphics::JobHandle Graphics::setupTextureAsync(const TextureSetupDesc& desc, TextureHandle& outTexture){
    auto payload = MakeCustomUnique<__hidden_graphics::TextureSetupJobData>(
        m_allocator.getObjectArena(),
        m_allocator.getObjectArena(),
        desc,
        outTexture
    );
    payload->uploadBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.data, desc.uploadDataSize);
    payload->setupDesc.data = nullptr;
    payload->setupDesc.uploadDataSize = payload->uploadBytes.size();

    return m_jobSystem.submit([this, payload = Move(payload)]() mutable{
        payload->setupDesc.data = payload->uploadBytes.empty() ? nullptr : payload->uploadBytes.data();
        payload->setupDesc.uploadDataSize = payload->uploadBytes.size();
        payload->outTexture = setupTexture(payload->setupDesc);
    });
}

Graphics::JobHandle Graphics::setupMeshAsync(const MeshSetupDesc& desc, MeshResource& outMesh){
    auto payload = MakeCustomUnique<__hidden_graphics::MeshSetupJobData>(
        m_allocator.getObjectArena(),
        m_allocator.getObjectArena(),
        desc,
        outMesh
    );
    payload->vertexBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.vertexData, desc.vertexDataSize);
    payload->indexBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.indexData, desc.indexDataSize);
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

usize Graphics::getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns)const{
    IDevice* device = getDevice();
    return device->getCoopVecMatrixSize(type, layout, rows, columns);
}

void Graphics::waitJob(JobHandle handle)const{
    if(!handle.valid())
        return;

    m_jobSystem.wait(handle);
}

void Graphics::waitAllJobs()const{
    m_jobSystem.waitAll();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
