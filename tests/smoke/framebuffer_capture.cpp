// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "framebuffer_capture.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>

#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_framebuffer_capture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Name s_BitmapScratchArena("tests/smoke/framebuffer_capture/bitmap");
static constexpr usize s_BitmapFileHeaderSize = 14u;
static constexpr usize s_BitmapInfoHeaderSize = 40u;
static constexpr usize s_BitmapPixelOffset = s_BitmapFileHeaderSize + s_BitmapInfoHeaderSize;
static constexpr usize s_BitmapBytesPerPixel = 3u;
static constexpr usize s_SourceBytesPerPixel = 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void WriteU16LE(const NotNull<u8*> destination, const u16 value)noexcept{
    destination.get()[0] = static_cast<u8>(value & 0xffu);
    destination.get()[1] = static_cast<u8>((value >> 8u) & 0xffu);
}

static void WriteU32LE(const NotNull<u8*> destination, const u32 value)noexcept{
    destination.get()[0] = static_cast<u8>(value & 0xffu);
    destination.get()[1] = static_cast<u8>((value >> 8u) & 0xffu);
    destination.get()[2] = static_cast<u8>((value >> 16u) & 0xffu);
    destination.get()[3] = static_cast<u8>((value >> 24u) & 0xffu);
}

static bool IsSupportedSdrFormat(const Core::Format::Enum format)noexcept{
    return format == Core::Format::RGBA8_UNORM || format == Core::Format::RGBA8_UNORM_SRGB || format == Core::Format::BGRA8_UNORM || format == Core::Format::BGRA8_UNORM_SRGB;
}

static bool IsBgraFormat(const Core::Format::Enum format)noexcept{
    return format == Core::Format::BGRA8_UNORM || format == Core::Format::BGRA8_UNORM_SRGB;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FramebufferCapture::ReadbackTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ Core::GpuQueueCapability::Transfer };


public:
    struct Payload{
        Core::TextureHandle source;
        Core::TextureSlice sourceSlice;
        Core::StagingTextureHandle destination;
        Core::TextureSlice destinationSlice;
        RefCountPtr<CompletionState> completionState;
        u64 graphicsFrameIndex = 0u;
        u64 generation = 0u;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(!payload.source || !payload.destination || !payload.completionState || context.commandIrCapture)
            return false;

        commandList.copyTexture(*payload.destination, payload.destinationSlice, *payload.source, payload.sourceSlice);
        return !commandList.commandRecordingFailed();
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        if(payload.completionState && payload.generation == payload.completionState->generation){
            payload.completionState->acceptedToken = token;
            payload.completionState->graphicsFrameIndex = payload.graphicsFrameIndex;
        }
    }

    static void Discarded(Payload& payload)noexcept{
        if(payload.completionState && payload.generation == payload.completionState->generation)
            payload.completionState->acceptedToken = {};
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FramebufferCapture::FramebufferCapture(ProjectRuntimeContext& context, const AStringView outputPath, const u32 captureFrameCount, const FramebufferCaptureOptions options)
    : FramebufferCapture(context, ::Path<Core::Alloc::GlobalArena>(context.objectArena, outputPath), captureFrameCount, options)
{}

FramebufferCapture::FramebufferCapture(ProjectRuntimeContext& context, ::Path<Core::Alloc::GlobalArena>&& outputPath, const u32 captureFrameCount, const FramebufferCaptureOptions options)
    : Core::IRenderPass(context.graphics)
    , m_context(context)
    , m_options(options)
    , m_outputPath(Move(outputPath))
    , m_completionState(MakeRefCount<CompletionState>())
    , m_captureFrameCount(captureFrameCount)
{
    if(m_outputPath.empty())
        markFailed(NWB_TEXT("capture output path is empty"));
    if(m_captureFrameCount == 0u)
        markFailed(NWB_TEXT("capture frame count must be greater than zero"));
}

FramebufferCapture::~FramebufferCapture()noexcept{
    stop();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FramebufferCapture::start(){
    if(m_registered)
        return true;
    if(m_failed || m_stopped)
        return false;

    Core::GraphicsRuntime& graphics = getGraphics();
    Core::IGpuTaskGraphPresentationContributor* const contributor = graphics.taskGraphPresentationContributor();
    if(contributor && contributor != this){
        markFailed(NWB_TEXT("another task-graph presentation contributor is already registered"));
        return false;
    }

    graphics.addRenderPassToBack(*this);
    if(m_failed){
        graphics.removeRenderPass(*this);
        return false;
    }
    graphics.setTaskGraphPresentationContributor(this);
    m_registered = true;
    return true;
}

void FramebufferCapture::stop(){
    if(!m_registered)
        return;

    Core::GraphicsRuntime& graphics = getGraphics();
    graphics.clearTaskGraphPresentationContributor(*this);
    m_registered = false;
    m_stopped = true;
    graphics.removeRenderPass(*this);
    resetPendingReadback();
}

void FramebufferCapture::update(){
    if(!m_registered)
        return;
    if(m_skipped || m_failed){
        requestTerminalQuit();
        return;
    }
    if(m_captureReady){
        if(m_options.quitWhenReady)
            requestTerminalQuit();
        return;
    }
    const Core::QueueSubmissionToken acceptedToken = m_completionState->acceptedToken;
    if(!acceptedToken.valid())
        return;
    if(!acceptedToken.hasPhysicalQueueIdentity()){
        markFailed(NWB_TEXT("accepted readback submission has no physical queue identity"));
        requestTerminalQuit();
        return;
    }

    Core::GraphicsBackend::Device& device = getGraphics().getDevice();
    if(acceptedToken.deviceGeneration != device.getDeviceGeneration()){
        markFailed(NWB_TEXT("accepted readback submission belongs to a stale graphics device"));
        requestTerminalQuit();
        return;
    }
    const Core::GpuPhysicalQueueId physicalQueue{ .index = acceptedToken.physicalQueueIndex, .deviceGeneration = acceptedToken.deviceGeneration, };
    const Expected<u64> completedValue = device.queueGetCompletedInstance(physicalQueue);
    if(!completedValue){
        markFailed(NWB_TEXT("failed to query readback submission completion"));
        requestTerminalQuit();
        return;
    }
    if(*completedValue < acceptedToken.value)
        return;
    if(!m_readback){
        markFailed(NWB_TEXT("accepted readback submission lost its staging texture"));
        requestTerminalQuit();
        return;
    }

    const auto mapping = device.mapStagingTexture(*m_readback, Core::TextureSlice{}, Core::CpuAccessMode::Read);
    if(!mapping){
        markFailed(NWB_TEXT("failed to map the completed readback staging texture"));
        requestTerminalQuit();
        return;
    }

    Core::Alloc::ScratchArena scratchArena(__hidden_framebuffer_capture::s_BitmapScratchArena);
    const bool captureWritten = writeCapture(MakeNotNull(static_cast<const u8*>(mapping->data)), mapping->rowPitch, scratchArena);
    device.unmapStagingTexture(*m_readback);
    if(!captureWritten){
        markFailed(NWB_TEXT("failed to write the framebuffer capture atomically"));
        requestTerminalQuit();
        return;
    }

    m_captureReady = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FramebufferCapture: capture ready"));
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FramebufferCapture: graphics source frame {}"), m_completionState->graphicsFrameIndex);
    if(m_options.quitWhenReady)
        requestTerminalQuit();
}

bool FramebufferCapture::rearm(::Path<Core::Alloc::GlobalArena>&& outputPath)noexcept{
    if(!m_registered || !m_captureReady || m_failed || m_skipped || m_quitRequested || outputPath.empty() || m_completionState->generation == Limit<u64>::s_Max)
        return false;

    m_outputPath = Move(outputPath);
    ++m_completionState->generation;
    m_completionState->acceptedToken = {};
    m_completionState->graphicsFrameIndex = Limit<u64>::s_Max;
    m_preparedFrame = {};
    m_preparedGraphGeneration = 0u;
    m_lastCountedGraphicsFrame = Limit<u64>::s_Max;
    m_preparedFrameCount = 0u;
    m_taskGraphPresentationPrepared = false;
    m_taskGraphPresentationClaimed = false;
    m_captureReady = false;
    return true;
}

void FramebufferCapture::finish(){
    NWB_ASSERT(m_captureReady);
    requestTerminalQuit();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FramebufferCapture::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    if(m_skipped || m_failed || width == 0u || height == 0u)
        return true;
    if((m_options.requiredWidth != 0u && width != m_options.requiredWidth) || (m_options.requiredHeight != 0u && height != m_options.requiredHeight))
        return true;
    if(!getGraphics().isSwapChainReadbackAvailable())
        return true;

    const Core::Texture* const backbuffer = getGraphics().getBackBuffer(0u);
    if(!backbuffer){
        markFailed(NWB_TEXT("swap-chain texture is unavailable during capture resource setup"));
        return false;
    }
    return prepareReadback(backbuffer->getCreationDescription());
}

void FramebufferCapture::invalidateResources(){
    if(!m_registered)
        return;

    if(m_captureReady){
        m_readback.reset();
        m_captureDescription = {};
    }else if(!m_skipped && !m_failed)
        resetPendingReadback();
}

void FramebufferCapture::backBufferResizing(){
    if(m_registered)
        invalidateResources();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FramebufferCapture::prepareTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame){
    m_taskGraphPresentationPrepared = false;
    if(m_captureReady || m_skipped || m_failed || m_completionState->acceptedToken.valid())
        return true;

    if(!getGraphics().isSwapChainReadbackAvailable()){
        m_skipped = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FramebufferCapture: skipped because swap-chain transfer-source usage is unavailable"));
        return true;
    }
    if(!frame.valid()){
        markFailed(NWB_TEXT("renderer supplied an invalid acquired presentation frame"));
        return true;
    }

    const u64 graphicsFrame = getGraphics().getFrameIndex();
    if(graphicsFrame != m_lastCountedGraphicsFrame){
        m_lastCountedGraphicsFrame = graphicsFrame;
        if(m_preparedFrameCount < m_captureFrameCount)
            ++m_preparedFrameCount;
    }
    if(m_preparedFrameCount < m_captureFrameCount)
        return true;
    const Core::TextureDesc& description = frame.backBuffer.texture->getCreationDescription();
    if((m_options.requiredWidth != 0u && description.width != m_options.requiredWidth) || (m_options.requiredHeight != 0u && description.height != m_options.requiredHeight))
        return true;
    if(m_options.shouldCapture && !m_options.shouldCapture(m_options.predicateContext, graphicsFrame))
        return true;

    if(!stagingMatches(description)){
        markFailed(NWB_TEXT("framebuffer readback staging texture was not prepared for the current swap chain"));
        return true;
    }

    m_preparedFrame = frame;
    m_taskGraphPresentationPrepared = true;
    m_taskGraphPresentationClaimed = false;
    m_preparedGraphGeneration = 0u;
    return true;
}

bool FramebufferCapture::hasTaskGraphPresentationWork()const noexcept{
    return m_taskGraphPresentationPrepared && m_readback && !m_completionState->acceptedToken.valid() && !m_captureReady && !m_skipped && !m_failed;
}

Expected<Core::GpuTaskId> FramebufferCapture::declareTaskGraphPresentation(Core::GpuTaskGraph& graph, const Core::AcquiredPresentationFrame& frame, const Core::GpuGraphResourceId backbuffer, const Core::GpuTaskId previousTask){
    u64 graphGeneration = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        graphGeneration = declarations.generation();
    }
    if(m_taskGraphPresentationClaimed && m_preparedGraphGeneration != graphGeneration){
        m_taskGraphPresentationClaimed = false;
        m_preparedGraphGeneration = 0u;
    }
    if(!m_taskGraphPresentationPrepared || m_taskGraphPresentationClaimed || !m_readback || m_completionState->acceptedToken.valid())
        return MakeUnexpected(Failure{});
    if(
        !frame.valid()
        || !previousTask.valid()
        || !backbuffer.valid()
        || frame.backBuffer.texture != m_preparedFrame.backBuffer.texture
        || frame.backBuffer.nativeInitialState != m_preparedFrame.backBuffer.nativeInitialState
        || frame.backBuffer.index != m_preparedFrame.backBuffer.index
        || frame.framebuffer != m_preparedFrame.framebuffer
    ){
        markFailed(NWB_TEXT("presentation declaration no longer matches the prepared acquired frame"));
        return MakeUnexpected(Failure{});
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    const auto stagingHazardResult = graph.importHazardDomain(Core::GpuGraphResourceDesc{}.setIdentity(Name("tests.smoke.framebuffer_capture.staging")).setMarkerLabel("Framebuffer Capture Staging").setType(Core::GpuGraphResourceType::HazardDomain));
    if(!stagingHazardResult){
        markFailed(NWB_TEXT("failed to declare the framebuffer staging hazard domain"));
        return MakeUnexpected(Failure{});
    }

    const Core::GpuGraphResourceId stagingHazard = *stagingHazardResult;

    Core::GpuTaskDesc taskDesc;
    const Core::GpuTaskResourceUse resourceUses[] = {
        { .resource = backbuffer, .range = { .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u) }, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, },
        { .resource = stagingHazard, .range = {}, .requiredState = Core::ResourceStates::Unknown, .access = Core::GpuTaskResourceAccess::Write, },
    };
    taskDesc.setIdentity(Name("tests.smoke.framebuffer_capture")).setMarkerLabel("Framebuffer Capture Readback").setScheduling(scheduling).setDependencies(&previousTask, 1u).setResourceUses(resourceUses, LengthOf(resourceUses));
    const auto readbackTask = graph.addTask<ReadbackTask>(
        taskDesc,
        ReadbackTask::Payload{ .source = frame.backBuffer.texture, .sourceSlice = {}, .destination = m_readback, .destinationSlice = {}, .completionState = m_completionState, .graphicsFrameIndex = m_lastCountedGraphicsFrame, .generation = m_completionState->generation, }
    );
    if(!readbackTask){
        markFailed(NWB_TEXT("failed to declare the framebuffer readback task"));
        return MakeUnexpected(Failure{});
    }

    m_taskGraphPresentationClaimed = true;
    m_preparedGraphGeneration = graphGeneration;
    return readbackTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FramebufferCapture::markFailed(const TStringView reason){
    if(m_failed)
        return;

    m_failed = true;
    NWB_LOGGER_ERROR(NWB_TEXT("FramebufferCapture: {}"), reason);
}

void FramebufferCapture::requestTerminalQuit(){
    if(m_quitRequested)
        return;

    m_quitRequested = true;
    m_context.requestQuit();
}

void FramebufferCapture::resetPendingReadback(){
    m_completionState = MakeRefCount<CompletionState>();
    m_readback.reset();
    m_captureDescription = {};
    m_preparedFrame = {};
    m_preparedGraphGeneration = 0u;
    m_taskGraphPresentationPrepared = false;
    m_taskGraphPresentationClaimed = false;
}

bool FramebufferCapture::stagingMatches(const Core::TextureDesc& description)const noexcept{
    return
        m_readback
        && m_captureDescription.width == description.width
        && m_captureDescription.height == description.height
        && m_captureDescription.depth == description.depth
        && m_captureDescription.arraySize == description.arraySize
        && m_captureDescription.mipLevels == description.mipLevels
        && m_captureDescription.sampleCount == description.sampleCount
        && m_captureDescription.sampleQuality == description.sampleQuality
        && m_captureDescription.format == description.format
        && m_captureDescription.dimension == description.dimension
        && m_captureDescription.queueSharing == description.queueSharing
    ;
}

bool FramebufferCapture::prepareReadback(const Core::TextureDesc& description){
    if(!__hidden_framebuffer_capture::IsSupportedSdrFormat(description.format)){
        markFailed(NWB_TEXT("swap-chain format is not SDR RGBA8/BGRA8 UNORM or SRGB"));
        return false;
    }
    if(description.width == 0u || description.height == 0u || description.depth != 1u || description.arraySize != 1u || description.mipLevels != 1u || description.sampleCount != 1u || description.dimension != Core::TextureDimension::Texture2D){
        markFailed(NWB_TEXT("swap-chain texture shape cannot be represented by the SDR framebuffer capture"));
        return false;
    }
    if(stagingMatches(description))
        return true;

    m_readback.reset();
    m_captureDescription = description;
    auto readbackResult = getGraphics().getDevice().createStagingTexture(description, Core::CpuAccessMode::Read);
    if(readbackResult)
        m_readback = Move(*readbackResult);
    else
        m_readback.reset();
    if(!readbackResult){
        markFailed(NWB_TEXT("failed to allocate the matching framebuffer readback staging texture"));
        return false;
    }
    return true;
}

bool FramebufferCapture::writeCapture(const NotNull<const u8*> sourceBytes, const usize sourceRowPitch, Core::Alloc::ScratchArena& scratchArena){
    const u32 width = m_captureDescription.width;
    const u32 height = m_captureDescription.height;
    if(width > static_cast<u32>(Limit<i32>::s_Max) || height > static_cast<u32>(Limit<i32>::s_Max))
        return false;

    const auto sourceRowBytes = TryMultiply<usize>(static_cast<usize>(width), __hidden_framebuffer_capture::s_SourceBytesPerPixel);
    if(!sourceRowBytes)
        return false;
    const auto bitmapRowBytes = TryMultiply<usize>(static_cast<usize>(width), __hidden_framebuffer_capture::s_BitmapBytesPerPixel);
    if(!bitmapRowBytes || sourceRowPitch < *sourceRowBytes || AddOverflows<usize>(*bitmapRowBytes, 3u))
        return false;

    const usize bitmapRowPitch = (*bitmapRowBytes + 3u) & ~usize(3u);
    const auto bitmapPixelBytes = TryMultiply<usize>(bitmapRowPitch, static_cast<usize>(height));
    if(!bitmapPixelBytes)
        return false;
    if(AddOverflows<usize>(__hidden_framebuffer_capture::s_BitmapPixelOffset, *bitmapPixelBytes))
        return false;

    const usize bitmapFileBytes = __hidden_framebuffer_capture::s_BitmapPixelOffset + *bitmapPixelBytes;
    if(*bitmapPixelBytes > static_cast<usize>(Limit<u32>::s_Max) || bitmapFileBytes > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    Vector<u8, Core::Alloc::ScratchArena> bitmapBytes(bitmapFileBytes, 0u, scratchArena);
    bitmapBytes[0] = static_cast<u8>('B');
    bitmapBytes[1] = static_cast<u8>('M');
    __hidden_framebuffer_capture::WriteU32LE(MakeNotNull(bitmapBytes.data() + 2u), static_cast<u32>(bitmapFileBytes));
    __hidden_framebuffer_capture::WriteU32LE(MakeNotNull(bitmapBytes.data() + 10u), static_cast<u32>(__hidden_framebuffer_capture::s_BitmapPixelOffset));
    __hidden_framebuffer_capture::WriteU32LE(MakeNotNull(bitmapBytes.data() + __hidden_framebuffer_capture::s_BitmapFileHeaderSize), static_cast<u32>(__hidden_framebuffer_capture::s_BitmapInfoHeaderSize));
    __hidden_framebuffer_capture::WriteU32LE(MakeNotNull(bitmapBytes.data() + 18u), width);
    __hidden_framebuffer_capture::WriteU32LE(MakeNotNull(bitmapBytes.data() + 22u), height);
    __hidden_framebuffer_capture::WriteU16LE(MakeNotNull(bitmapBytes.data() + 26u), 1u);
    __hidden_framebuffer_capture::WriteU16LE(MakeNotNull(bitmapBytes.data() + 28u), 24u);
    __hidden_framebuffer_capture::WriteU32LE(MakeNotNull(bitmapBytes.data() + 34u), static_cast<u32>(*bitmapPixelBytes));

    const bool sourceIsBgra = __hidden_framebuffer_capture::IsBgraFormat(m_captureDescription.format);
    for(u32 bitmapY = 0u; bitmapY < height; ++bitmapY){
        const usize sourceY = static_cast<usize>(height - 1u - bitmapY);
        const auto sourceRowOffset = TryMultiply<usize>(sourceY, sourceRowPitch);
        if(!sourceRowOffset)
            return false;
        const auto sourceRow = MakeNotNull(sourceBytes.get() + *sourceRowOffset);
        const auto bitmapRow = MakeNotNull(bitmapBytes.data() + __hidden_framebuffer_capture::s_BitmapPixelOffset + static_cast<usize>(bitmapY) * bitmapRowPitch);
        for(u32 x = 0u; x < width; ++x){
            const auto sourcePixel = MakeNotNull(sourceRow.get() + static_cast<usize>(x) * __hidden_framebuffer_capture::s_SourceBytesPerPixel);
            const auto bitmapPixel = MakeNotNull(bitmapRow.get() + static_cast<usize>(x) * __hidden_framebuffer_capture::s_BitmapBytesPerPixel);
            bitmapPixel.get()[0] = sourceIsBgra ? sourcePixel.get()[0] : sourcePixel.get()[2];
            bitmapPixel.get()[1] = sourcePixel.get()[1];
            bitmapPixel.get()[2] = sourceIsBgra ? sourcePixel.get()[2] : sourcePixel.get()[0];
        }
    }

    const ::Path<Core::Alloc::ScratchArena> outputPath(scratchArena, m_outputPath);
    {
        const auto outputDirectory = outputPath.parentPath();
        if(!outputDirectory.empty() && !EnsureDirectories(outputDirectory))
            return false;
    }

    ::Path<Core::Alloc::ScratchArena> partialPath(scratchArena, outputPath);
    partialPath += NWB_TEXT(".partial");
    if(!WriteBinaryFile(partialPath, bitmapBytes)){
        const auto removed = RemoveFile(partialPath);
        if(!removed)
            NWB_LOGGER_WARNING(NWB_TEXT("FramebufferCapture: failed to remove an incomplete partial capture"));
        return false;
    }
    if(!RenamePath(partialPath, outputPath)){
        const auto removed = RemoveFile(partialPath);
        if(!removed)
            NWB_LOGGER_WARNING(NWB_TEXT("FramebufferCapture: failed to remove an uncommitted partial capture"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

