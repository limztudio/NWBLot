// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics.h"

#include <core/input/input.h>
#include <logger/client/logger.h>

#include "vulkan/vulkan_backend_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UploadBytesAllocator = Alloc::CustomAllocator<u8>;
using UploadBytes = Vector<u8, UploadBytesAllocator>;


static bool ComputeTextureUploadByteSize(const Graphics::TextureSetupDesc& desc, usize& outRequiredBytes){
    outRequiredBytes = 0;

    const TextureDesc& textureDesc = desc.textureDesc;
    if(textureDesc.width == 0 || textureDesc.height == 0 || textureDesc.depth == 0 || textureDesc.mipLevels == 0 || textureDesc.arraySize == 0)
        return false;
    if(textureDesc.sampleCount != 1)
        return false;
    if(desc.mipLevel >= textureDesc.mipLevels || desc.arraySlice >= textureDesc.arraySize)
        return false;
    if(static_cast<usize>(textureDesc.format) >= static_cast<usize>(Format::kCount))
        return false;

    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    const u32 formatBlockWidth = GetFormatBlockWidth(formatInfo);
    const u32 formatBlockHeight = GetFormatBlockHeight(formatInfo);
    if(formatBlockWidth == 0 || formatBlockHeight == 0 || formatInfo.bytesPerBlock == 0)
        return false;

    const u32 width = Max<u32>(1u, textureDesc.width >> desc.mipLevel);
    const u32 height = Max<u32>(1u, textureDesc.height >> desc.mipLevel);
    const u32 depth = Max<u32>(1u, textureDesc.depth >> desc.mipLevel);

    const u64 blockCountX = DivideUp(static_cast<u64>(width), static_cast<u64>(formatBlockWidth));
    const u64 blockCountY = DivideUp(static_cast<u64>(height), static_cast<u64>(formatBlockHeight));
    if(blockCountX > Limit<u64>::s_Max / formatInfo.bytesPerBlock)
        return false;

    const u64 naturalRowPitch = blockCountX * formatInfo.bytesPerBlock;
    const u64 effectiveRowPitch = desc.rowPitch != 0 ? static_cast<u64>(desc.rowPitch) : naturalRowPitch;
    if(effectiveRowPitch == 0 || effectiveRowPitch < naturalRowPitch || (effectiveRowPitch % formatInfo.bytesPerBlock) != 0)
        return false;
    if(blockCountY > Limit<u64>::s_Max / effectiveRowPitch)
        return false;

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = desc.depthPitch != 0 ? static_cast<u64>(desc.depthPitch) : packedSlicePitch;
    if(effectiveDepthPitch == 0 || effectiveDepthPitch < packedSlicePitch || (effectiveDepthPitch % effectiveRowPitch) != 0)
        return false;

    if(depth > 1 && static_cast<u64>(depth - 1) > (Limit<u64>::s_Max - packedSlicePitch) / effectiveDepthPitch)
        return false;

    const u64 requiredBytes = depth > 1
        ? effectiveDepthPitch * static_cast<u64>(depth - 1) + packedSlicePitch
        : packedSlicePitch
    ;
    if(requiredBytes > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    outRequiredBytes = static_cast<usize>(requiredBytes);
    return true;
}

static bool ValidateBufferSetupUpload(const Graphics::BufferSetupDesc& desc){
    if(desc.dataSize == 0)
        return true;
    if(!desc.data){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up buffer '{}': upload data is null"), StringConvert(desc.bufferDesc.debugName.c_str()));
        return false;
    }
    if(desc.destOffsetBytes > desc.bufferDesc.byteSize || static_cast<u64>(desc.dataSize) > desc.bufferDesc.byteSize - desc.destOffsetBytes){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Graphics: failed to set up buffer '{}': upload range offset {} size {} exceeds buffer size {}"),
            StringConvert(desc.bufferDesc.debugName.c_str()),
            desc.destOffsetBytes,
            static_cast<u64>(desc.dataSize),
            desc.bufferDesc.byteSize
        );
        return false;
    }

    return true;
}

static bool ValidateTextureSetupUpload(const Graphics::TextureSetupDesc& desc){
    if(!desc.data && desc.uploadDataSize == 0)
        return true;
    if(!desc.data || desc.uploadDataSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up texture '{}': upload data and size must both be provided"), StringConvert(desc.textureDesc.name.c_str()));
        return false;
    }

    usize requiredBytes = 0;
    if(!ComputeTextureUploadByteSize(desc, requiredBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up texture '{}': invalid upload layout"), StringConvert(desc.textureDesc.name.c_str()));
        return false;
    }
    if(desc.uploadDataSize < requiredBytes){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Graphics: failed to set up texture '{}': upload data size {} is smaller than required size {}"),
            StringConvert(desc.textureDesc.name.c_str()),
            desc.uploadDataSize,
            requiredBytes
        );
        return false;
    }

    return true;
}

static bool ValidateMeshSetupDesc(const Graphics::MeshSetupDesc& desc){
    if(!desc.vertexData || desc.vertexDataSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': vertex data is missing"), StringConvert(desc.vertexBufferName.c_str()));
        return false;
    }
    if(desc.vertexStride == 0 || (desc.vertexDataSize % static_cast<usize>(desc.vertexStride)) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': vertex data size is not aligned to vertex stride"), StringConvert(desc.vertexBufferName.c_str()));
        return false;
    }

    const usize vertexCount = desc.vertexDataSize / static_cast<usize>(desc.vertexStride);
    if(vertexCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': vertex count exceeds u32 range"), StringConvert(desc.vertexBufferName.c_str()));
        return false;
    }

    if((desc.indexData == nullptr) != (desc.indexDataSize == 0)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': index data and size must both be provided"), StringConvert(desc.indexBufferName.c_str()));
        return false;
    }
    if(desc.indexDataSize > 0){
        const usize indexStride = desc.use32BitIndices ? sizeof(u32) : sizeof(u16);
        if((desc.indexDataSize % indexStride) != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': index data size is not aligned to index stride"), StringConvert(desc.indexBufferName.c_str()));
            return false;
        }
        const usize indexCount = desc.indexDataSize / indexStride;
        if(indexCount > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': index count exceeds u32 range"), StringConvert(desc.indexBufferName.c_str()));
            return false;
        }
    }

    return true;
}


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

template<typename JobData, typename Desc, typename Output, typename Validate, typename ConfigurePayload, typename ExecutePayload>
static Graphics::JobHandle SubmitSetupUploadJob(
    Graphics& graphics,
    Alloc::CustomArena& arena,
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

    auto payload = MakeCustomUnique<JobData>(arena, arena, desc, output);
    configurePayload(*payload, arena);

    return jobSystem.submit([&graphics, payload = Move(payload), executePayload = Forward<ExecutePayload>(executePayload)]() mutable{
        executePayload(graphics, *payload);
    });
}

static void ConfigureBufferSetupPayload(BufferSetupJobData& payload, Alloc::CustomArena& arena){
    payload.uploadBytes = CopyBytes(arena, payload.setupDesc.data, payload.setupDesc.dataSize);
    payload.setupDesc.data = nullptr;
    payload.setupDesc.dataSize = payload.uploadBytes.size();
}

static void ExecuteBufferSetupPayload(Graphics& graphics, BufferSetupJobData& payload){
    payload.setupDesc.data = payload.uploadBytes.empty() ? nullptr : payload.uploadBytes.data();
    payload.setupDesc.dataSize = payload.uploadBytes.size();
    payload.outBuffer = graphics.setupBuffer(payload.setupDesc);
}

static void ConfigureTextureSetupPayload(TextureSetupJobData& payload, Alloc::CustomArena& arena){
    payload.uploadBytes = CopyBytes(arena, payload.setupDesc.data, payload.setupDesc.uploadDataSize);
    payload.setupDesc.data = nullptr;
    payload.setupDesc.uploadDataSize = payload.uploadBytes.size();
}

static void ExecuteTextureSetupPayload(Graphics& graphics, TextureSetupJobData& payload){
    payload.setupDesc.data = payload.uploadBytes.empty() ? nullptr : payload.uploadBytes.data();
    payload.setupDesc.uploadDataSize = payload.uploadBytes.size();
    payload.outTexture = graphics.setupTexture(payload.setupDesc);
}

static void AddVulkanDeviceExtensionOnce(Vector<AString>& extensions, const char* extensionName){
    for(const auto& extension : extensions){
        if(NWB_STRCMP(extension.c_str(), extensionName) == 0)
            return;
    }
    extensions.push_back(extensionName);
}

static bool CopyInstanceParameters(DeviceCreationParameters& dst, const InstanceParameters& src){
    if(src.enableDebugRuntime && !CanEnableDebugRuntime())
        return false;

    static_cast<InstanceParameters&>(dst) = src;
    return true;
}

static CustomUniquePtr<IGraphicsBackend> CreateDefaultBackend(
    const DeviceCreationParameters& deviceParams,
    SwapChainRuntimeState& swapChainState,
    GraphicsAllocator& allocator,
    Alloc::ThreadPool& threadPool
){
    return MakeCustomUnique<Vulkan::BackendContext>(allocator.getObjectArena(), deviceParams, swapChainState, allocator, threadPool);
}

static const Vulkan::IBackendQueries* GetVulkanBackendQueries(const IGraphicsBackend& backend){
    if(backend.getGraphicsAPI() != GraphicsAPI::VULKAN)
        return nullptr;

    return static_cast<const Vulkan::IBackendQueries*>(backend.queryInterface(Vulkan::s_BackendQueriesInterfaceID));
}

constexpr bool IsFp16CoopVecFormat(const CooperativeVectorMatMulFormatCombo& combo){
    return
        combo.inputType == CooperativeVectorDataType::Float16
        && combo.inputInterpretation == CooperativeVectorDataType::Float16
        && combo.matrixInterpretation == CooperativeVectorDataType::Float16
        && combo.outputType == CooperativeVectorDataType::Float16
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Graphics::BackBufferResizingCallback(void* userData){
    if(auto* graphics = static_cast<Graphics*>(userData))
        graphics->backBufferResizing();
}

void Graphics::BackBufferResizedCallback(void* userData){
    if(auto* graphics = static_cast<Graphics*>(userData))
        graphics->backBufferResized();
}


Graphics::Graphics(GraphicsAllocator& allocator, Alloc::ThreadPool& threadPool, Alloc::JobSystem& jobSystem, InputDispatcher& input)
    : m_allocator(allocator)
    , m_threadPool(threadPool)
    , m_jobSystem(jobSystem)
    , m_input(input)
    , m_backend(__hidden_graphics::CreateDefaultBackend(m_deviceCreationParams, m_swapChainState, m_allocator, m_threadPool))
    , m_renderPasses(RenderPassListAllocator(m_allocator.getObjectArena()))
    , m_swapChainFramebuffers(SwapChainFramebufferVectorAllocator(m_allocator.getObjectArena()))
{
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;
    syncInputMousePositionScale();
    __hidden_graphics::AddVulkanDeviceExtensionOnce(m_deviceCreationParams.optionalVulkanDeviceExtensions, "VK_NV_cooperative_vector");
}
Graphics::~Graphics(){
    destroy();
}

bool Graphics::init(const Common::FrameData& data){
    m_deviceCreationParams.headlessDevice = false;
    m_hasPresentedFrame = false;

    m_swapChainState.backBufferWidth = data.width();
    m_swapChainState.backBufferHeight = data.height();
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;

    IGraphicsBackend& backend = *m_backend;
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

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Graphics: window device and swap chain created ({}x{})")
        , data.width()
        , data.height()
    );
    return true;
}

bool Graphics::createHeadlessDevice(){
    m_deviceCreationParams.headlessDevice = true;
    m_hasPresentedFrame = false;

    IGraphicsBackend& backend = *m_backend;
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
    if(!__hidden_graphics::CopyInstanceParameters(m_deviceCreationParams, params)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: debug runtime is only available in dbg builds"));
        return false;
    }

    if(!m_backend->createInstance())
        return false;

    m_instanceCreated = true;
    return true;
}

bool Graphics::setDebugRuntimeEnabled(bool enabled){
    if(enabled && !CanEnableDebugRuntime())
        return false;
    if(m_instanceCreated && m_deviceCreationParams.enableDebugRuntime != enabled)
        return false;

    m_deviceCreationParams.enableDebugRuntime = enabled;
    return true;
}

void Graphics::setPipelineCacheDirectory(const Path& directory){
    m_deviceCreationParams.pipelineCacheDirectory = directory;
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
    ){
        backBufferResizing();

        m_swapChainState.backBufferWidth = width;
        m_swapChainState.backBufferHeight = height;
        m_swapChainState.vsyncEnabled = m_requestedVSync;

        m_backend->resizeSwapChain();
        backBufferResized();
    }

    m_swapChainState.vsyncEnabled = m_requestedVSync;
}

void Graphics::destroy(){
    waitAllJobs();

    m_renderPasses.clear();

    m_swapChainFramebuffers.clear();
    m_backend->destroy();
    m_instanceCreated = false;
}

IDevice* Graphics::getDevice()const noexcept{
    return m_backend->getDevice();
}

bool Graphics::enumerateAdapters(Vector<AdapterInfo>& outAdapters){
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
    return m_backend->getRendererString();
}

GraphicsAPI::Enum Graphics::getGraphicsAPI()const{
    return m_backend->getGraphicsAPI();
}

void Graphics::reportLiveObjects()const{
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

void Graphics::setWindowTitle(NotNull<const tchar*> title){
    if(m_windowTitle == title.get())
        return;

    m_windowTitle = title.get();
}

ITexture* Graphics::getCurrentBackBuffer()const{
    return m_backend->getCurrentBackBuffer();
}

ITexture* Graphics::getBackBuffer(u32 index)const{
    return m_backend->getBackBuffer(index);
}

u32 Graphics::getCurrentBackBufferIndex()const{
    return m_backend->getCurrentBackBufferIndex();
}

u32 Graphics::getBackBufferCount()const{
    return m_backend->getBackBufferCount();
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
    const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(*m_backend);
    return queries && queries->isInstanceExtensionEnabled(extensionName);
}

bool Graphics::isVulkanDeviceExtensionEnabled(const char* extensionName)const{
    const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(*m_backend);
    return queries && queries->isDeviceExtensionEnabled(extensionName);
}

bool Graphics::isVulkanLayerEnabled(const char* layerName)const{
    const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(*m_backend);
    return queries && queries->isLayerEnabled(layerName);
}

void Graphics::getEnabledVulkanInstanceExtensions(Vector<AString>& extensions)const{
    if(const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(*m_backend)){
        queries->getEnabledInstanceExtensions(extensions);
        return;
    }

    extensions.clear();
}

void Graphics::getEnabledVulkanDeviceExtensions(Vector<AString>& extensions)const{
    if(const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(*m_backend)){
        queries->getEnabledDeviceExtensions(extensions);
        return;
    }

    extensions.clear();
}

void Graphics::getEnabledVulkanLayers(Vector<AString>& layers)const{
    if(const Vulkan::IBackendQueries* queries = __hidden_graphics::GetVulkanBackendQueries(*m_backend)){
        queries->getEnabledLayers(layers);
        return;
    }

    layers.clear();
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
    syncInputMousePositionScale();

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

void Graphics::syncInputMousePositionScale(){
    if(m_deviceCreationParams.supportExplicitDisplayScaling)
        m_input.setMousePositionScale(1.f, 1.f);
    else
        m_input.setMousePositionScale(m_dpiScaleFactorX, m_dpiScaleFactorY);
}

bool Graphics::shouldRenderUnfocused()const{
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->shouldRenderUnfocused())
            return true;
    }
    return false;
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
            if(m_backend->beginFrame(resizeCallbacks)){
                render();

                if(!m_backend->present())
                    return false;

                m_hasPresentedFrame = true;
            }
        }
    }

    YieldThread();

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
    if(!__hidden_graphics::ValidateBufferSetupUpload(desc))
        return {};

    BufferHandle buffer = device->createBuffer(desc.bufferDesc);
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to create setup buffer '{}'"), StringConvert(desc.bufferDesc.debugName.c_str()));
        return {};
    }

    if(!desc.data || desc.dataSize == 0)
        return buffer;

    CommandListParameters cmdParams;
    cmdParams.setQueueType(desc.queue);
    CommandListHandle commandList = device->createCommandList(cmdParams);
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to create upload command list for buffer '{}'"), StringConvert(desc.bufferDesc.debugName.c_str()));
        return {};
    }

    commandList->open();
    commandList->writeBuffer(buffer.get(), desc.data, desc.dataSize, desc.destOffsetBytes);
    commandList->close();
    ICommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1, desc.queue);

    return buffer;
}

TextureHandle Graphics::setupTexture(const TextureSetupDesc& desc)const{
    IDevice* device = getDevice();
    if(!__hidden_graphics::ValidateTextureSetupUpload(desc))
        return {};

    TextureHandle texture = device->createTexture(desc.textureDesc);
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to create setup texture '{}'"), StringConvert(desc.textureDesc.name.c_str()));
        return {};
    }

    if(!desc.data || desc.uploadDataSize == 0)
        return texture;

    CommandListParameters cmdParams;
    cmdParams.setQueueType(desc.queue);
    CommandListHandle commandList = device->createCommandList(cmdParams);
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to create upload command list for texture '{}'"), StringConvert(desc.textureDesc.name.c_str()));
        return {};
    }

    commandList->open();
    commandList->writeTexture(texture.get(), desc.arraySlice, desc.mipLevel, desc.data, desc.rowPitch, desc.depthPitch);
    commandList->close();
    ICommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1, desc.queue);

    return texture;
}

Graphics::MeshResource Graphics::setupMesh(const MeshSetupDesc& desc)const{
    if(!__hidden_graphics::ValidateMeshSetupDesc(desc))
        return {};

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
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh vertex buffer '{}'"), StringConvert(desc.vertexBufferName.c_str()));
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
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh index buffer '{}'"), StringConvert(desc.indexBufferName.c_str()));
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
    return __hidden_graphics::SubmitSetupUploadJob<__hidden_graphics::BufferSetupJobData>(
        *this,
        m_allocator.getObjectArena(),
        m_jobSystem,
        desc,
        outBuffer,
        __hidden_graphics::ValidateBufferSetupUpload,
        __hidden_graphics::ConfigureBufferSetupPayload,
        __hidden_graphics::ExecuteBufferSetupPayload
    );
}

Graphics::JobHandle Graphics::setupTextureAsync(const TextureSetupDesc& desc, TextureHandle& outTexture){
    return __hidden_graphics::SubmitSetupUploadJob<__hidden_graphics::TextureSetupJobData>(
        *this,
        m_allocator.getObjectArena(),
        m_jobSystem,
        desc,
        outTexture,
        __hidden_graphics::ValidateTextureSetupUpload,
        __hidden_graphics::ConfigureTextureSetupPayload,
        __hidden_graphics::ExecuteTextureSetupPayload
    );
}

Graphics::JobHandle Graphics::setupMeshAsync(const MeshSetupDesc& desc, MeshResource& outMesh){
    if(!__hidden_graphics::ValidateMeshSetupDesc(desc)){
        outMesh = {};
        return {};
    }

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

